#include "MacFloppy.h"
#include "../Data/Block.h"
#include <functional>
#include <unistd.h>

namespace s5
{
static const char* kScript = R"SH(#!/bin/sh
# usage: floppy.sh read|write DISKID IMAGE PROGRESS ABORTFILE VERIFY
MODE="$1"; ID="$2"; IMG="$3"; PROG="$4"; ABORT="$5"; VERIFY="$6"; LIMIT="${7:-160}"
TRACK=4608
case "$ID" in disk[0-9]|disk[0-9][0-9]) ;; *) echo "ERR Bad disk id" > "$PROG"; exit 1;; esac
if [ -z "$S5_TEST_DEV" ]; then
INFO=$(/usr/sbin/diskutil info "/dev/$ID")
echo "$INFO" | grep -q "(737280 Bytes)" || { echo "ERR Not a 720 KB disk" > "$PROG"; exit 1; }
echo "$INFO" | grep -qi "External\|Removable" || { echo "ERR Not an external disk" > "$PROG"; exit 1; }
/usr/sbin/diskutil unmountDisk "/dev/$ID" >/dev/null 2>&1
fi
DEV="/dev/r$ID"; [ -n "$S5_TEST_DEV" ] && DEV="$S5_TEST_DEV"
i=0; BAD=0
if [ "$MODE" = "write" ]; then
  while [ $i -lt 160 ]; do
    [ -e "$ABORT" ] && { echo "ERR Aborted" > "$PROG"; exit 1; }
    echo "W $i" > "$PROG"
    OUT=$(/bin/dd if="$IMG" of="$DEV" bs=$TRACK skip=$i seek=$i count=1 2>&1) || { echo "ERR Write failed on track $i: $(echo "$OUT" | /usr/bin/head -n 2 | /usr/bin/tr '\n' ' ')" > "$PROG"; exit 1; }
    i=$((i+1))
  done
  if [ "$VERIFY" = "1" ]; then
    i=0
    while [ $i -lt 160 ]; do
      [ -e "$ABORT" ] && { echo "ERR Aborted" > "$PROG"; exit 1; }
      echo "V $i" > "$PROG"
      A=$(/bin/dd if="$IMG" bs=$TRACK skip=$i count=1 2>/dev/null | /usr/bin/cksum)
      B=$(/bin/dd if="$DEV" bs=$TRACK skip=$i count=1 2>/dev/null | /usr/bin/cksum)
      [ "$A" = "$B" ] || { echo "ERR Verify failed on track $i" > "$PROG"; exit 1; }
      i=$((i+1))
    done
  fi
else
  GOOD=0
  while [ $i -lt $LIMIT ]; do
    [ -e "$ABORT" ] && { echo "ERR Aborted" > "$PROG"; exit 1; }
    echo "W $i" > "$PROG"
    CH="$IMG.chunk"
    if ! /bin/dd if="$DEV" of="$CH" bs=$TRACK skip=$i count=1 2>"$IMG.err" || [ "$(( $(/usr/bin/wc -c < "$CH") ))" -ne $TRACK ]; then
      # the whole track did not come at once (I/O error, flaky seek): sector by sector, 3 tries each
      : > "$CH"; s=0
      while [ $s -lt 9 ]; do
        n=0; got=0
        while [ $n -lt 3 ]; do
          if /bin/dd if="$DEV" of="$CH.s" bs=512 skip=$((i*9+s)) count=1 2>"$IMG.err" && [ "$(( $(/usr/bin/wc -c < "$CH.s") ))" -eq 512 ]; then got=1; GOOD=$((GOOD+1)); break; fi
          n=$((n+1))
        done
        if [ $got -eq 1 ]; then /bin/cat "$CH.s" >> "$CH"
        else
          /usr/bin/head -c 512 /dev/zero >> "$CH"; echo "$i $s" >> "$IMG.bad"
          [ $i -ge 14 ] && BAD=$((BAD+1))                      # tracks 0-13 = system area (before the first patch): not needed for the sounds
          if [ $BAD -gt 40 ]; then echo "ERR Too many unreadable sectors (track $i): $(/usr/bin/head -n 1 "$IMG.err")" > "$PROG"; exit 1; fi
        fi
        s=$((s+1))
      done
    else GOOD=$((GOOD+1))
    fi
    /bin/cat "$CH" >> "$IMG.part"
    i=$((i+1))
  done
  [ $GOOD -eq 0 ] && { echo "ERR Nothing readable: $(/usr/bin/head -n 1 "$IMG.err")" > "$PROG"; exit 1; }        # every sector failed: not a disk with bad spots, but no access
  [ $LIMIT -lt 160 ] && /bin/dd if=/dev/zero bs=$TRACK count=$((160-LIMIT)) 2>/dev/null >> "$IMG.part"        # partial read: the rest stays empty
  /bin/mv "$IMG.part" "$IMG"
fi
[ -z "$S5_TEST_DEV" ] && /usr/sbin/diskutil mountDisk "/dev/$ID" >/dev/null 2>&1
echo "OK" > "$PROG"
)SH";

// Root helper: started once with the password dialog, then it runs every floppy job of this session without asking again.
// It only takes "mode id verify" from the job file and works on fixed file names in the private session folder; it ends when the host program ends.
static const char* kHelper = R"SH(#!/bin/sh
DIR="$1"; PARENT="$2"
# the sign of life runs on its own: a floppy job takes a minute, and the plugin must see that the helper is still there
( while kill -0 "$PARENT" 2>/dev/null && [ ! -e "$DIR/quit" ]; do /usr/bin/touch "$DIR/alive"; /bin/sleep 1; done ) &
while kill -0 "$PARENT" 2>/dev/null; do
  [ -e "$DIR/quit" ] && exit 0
  if [ -f "$DIR/job" ]; then
    read MODE ID VERIFY LIMIT < "$DIR/job"
    /bin/rm -f "$DIR/job"
    case "$MODE" in read|write) ;; *) MODE=bad;; esac
    case "$VERIFY" in 0|1) ;; *) VERIFY=0;; esac
    case "$LIMIT" in ""|*[!0-9]*) LIMIT=160;; esac
    [ "$LIMIT" -gt 160 ] && LIMIT=160
    if [ "$MODE" = "bad" ]; then echo "ERR Bad job" > "$DIR/progress.txt"
    else /bin/sh "$DIR/floppy.sh" "$MODE" "$ID" "$DIR/disk.img" "$DIR/progress.txt" "$DIR/abort" "$VERIFY" "$LIMIT"
    fi
  fi
  /bin/sleep 0.2
done
)SH";

MacFloppy::MacFloppy() : juce::Thread ("S5 floppy")
{
   #if JUCE_MAC
    startThread();
   #endif
}

MacFloppy::~MacFloppy()
{
    abort();
    if (helperDir != juce::File()) helperDir.getChildFile ("quit").create();     // the root helper ends
    stopThread (5000);
    if (helperDir != juce::File()) { juce::Thread::sleep (400); helperDir.deleteRecursively(); }
}

void MacFloppy::scan()
{
   #if JUCE_MAC
    // One quick `diskutil list` is enough: a 720 KB medium is listed as "737.3 KB".  (`diskutil info` of every big disk and
    // `ioreg -l` can take seconds - a timeout must never be taken for "no drive": then the last known state stays.)
    juce::String found;
    int st = (int) State::NoDrive;
    bool ok = false;
    {
        juce::ChildProcess p;
        if (p.start ({ "/usr/sbin/diskutil", "list", "-plist", "external", "physical" }))
        {
            if (p.waitForProcessToFinish (15000))
            {
                ok = true;
                const auto xml = p.readAllProcessOutput();         // language independent: a disk of exactly 737280 bytes is a 720 KB floppy
                int pos = 0;
                while ((pos = xml.indexOf (pos, "<key>Size</key>")) >= 0)
                {
                    if (xml.substring (pos + 15).trimStart().startsWith ("<integer>737280</integer>"))
                    {
                        const auto before = xml.substring (0, pos);
                        const int k = before.lastIndexOf ("<key>DeviceIdentifier</key>");
                        if (k >= 0)
                        {
                            const auto id = before.substring (k + 27).fromFirstOccurrenceOf ("<string>", false, false).upToFirstOccurrenceOf ("</string>", false, false).trim();
                            if (id.startsWith ("disk")) { found = id; break; }
                        }
                    }
                    pos += 15;
                }
            }
            else p.kill();
        }
    }
    if (! ok) return;                                  // could not ask: keep the old state
    if (found.isEmpty())
    {
        // An empty drive is only visible as a USB device: any USB interface of the mass-storage class with the UFI sub-class (= floppy)
        // on any computer, plus a few well known names.
        bool drive = false;
        for (const char* cls : { "IOUSBHostInterface", "IOUSBInterface" })
        {
            juce::ChildProcess q;
            if (q.start ({ "/usr/sbin/ioreg", "-r", "-c", cls, "-w0" }) && q.waitForProcessToFinish (15000))
            {
                const auto t = q.readAllProcessOutput().toLowerCase();
                int pos = 0;
                while ((pos = t.indexOf (pos, "\"binterfaceclass\" = 8")) >= 0)         // look at the same interface block for sub class 4
                {
                    const auto blk = t.substring (pos, pos + 400);
                    if (blk.contains ("\"binterfacesubclass\" = 4")) { drive = true; break; }
                    pos += 20;
                }
            }
            if (drive) break;
        }
        if (! drive)
        {
            juce::ChildProcess q;
            if (q.start ({ "/usr/sbin/ioreg", "-p", "IOUSB", "-l", "-w0" }) && q.waitForProcessToFinish (15000))
            {
                const auto t = q.readAllProcessOutput().toLowerCase();
                drive = t.contains ("floppy") || t.contains ("fd-05pub") || t.contains ("fd05pub") || t.contains ("teac") || t.contains ("mpf82e") || t.contains ("pa905");
            }
            else return;
        }
        st = drive ? (int) State::NoDisk : (int) State::NoDrive;
    }
    {
        std::lock_guard<std::mutex> g (lock);
        diskId = found.toStdString();
    }
    const int ns = found.isNotEmpty() ? (int) State::Ready : st;
    if (stateValue.load() != (int) State::Busy && stateValue.exchange (ns) != ns) ++serial;
   #endif
}

void MacFloppy::run()
{
    while (! threadShouldExit())
    {
        bool doJob = false, w = false, v = false; int lim = 160;
        std::vector<uint8_t> img;
        {
            std::lock_guard<std::mutex> g (lock);
            if (jobPending) { doJob = true; w = jobWrite; v = jobVerify; lim = jobLimit; img = std::move (jobImage); jobPending = false; }
        }
        if (doJob) runJob (w, std::move (img), v, lim);
        else { scan(); wait (2000); }
    }
}

bool MacFloppy::start (bool write, const std::vector<uint8_t>& image, bool verify, int limit)
{
    if (state() != State::Ready) return false;
    if (write && (int) image.size() != data::kImageSize) return false;
    {
        std::lock_guard<std::mutex> g (lock);
        if (jobPending) return false;
        jobPending = true; jobWrite = write; jobVerify = verify; jobImage = image; jobLimit = write ? 160 : std::max (1, std::min (160, limit));
        pub = {}; pub.write = write; pub.limit = jobLimit;
        shown = pub;
    }
    stateValue = (int) State::Busy;
    ++serial;
    notify();
    return true;
}

void MacFloppy::abort()
{
    if (abortFile != juce::File())
        abortFile.create();                          // the script stops between two tracks
}

void MacFloppy::publish (const Job& j)
{
    std::lock_guard<std::mutex> g (lock);
    pub = j;
    ++serial;
}

void MacFloppy::runJob (bool write, std::vector<uint8_t> image, bool verify, int limit)
{
    std::string id;
    { std::lock_guard<std::mutex> g (lock); id = diskId; }

    Job j; j.write = write; j.limit = limit;
    auto fail = [&] (const juce::String& m) { j.finished = true; j.ok = false; j.error = m.toStdString(); publish (j); stateValue = (int) State::NoDisk; ++serial; };

    if (id.empty()) { fail ("No disk"); return; }

    if (helperDir == juce::File())
    {
        helperDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("s5floppy_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
        helperDir.createDirectory();
        juce::ChildProcess c; c.start ({ "/bin/chmod", "700", helperDir.getFullPathName() }); c.waitForProcessToFinish (2000);
    }
    auto tmp = helperDir;
    for (auto* n : { "disk.img", "disk.img.part", "disk.img.bad", "disk.img.err", "disk.img.chunk", "disk.img.chunk.s", "progress.txt", "abort", "job" })
        tmp.getChildFile (n).deleteFile();                       // leftovers of an earlier job
    auto script = tmp.getChildFile ("floppy.sh");
    auto img = tmp.getChildFile ("disk.img");
    auto prog = tmp.getChildFile ("progress.txt");
    abortFile = tmp.getChildFile ("abort");
    script.replaceWithText (kScript, false, false, "\n");          // JUCE would write CR LF by default: sh then fails with "syntax error near unexpected token newline"
    prog.replaceWithText ("WAIT");
    if (write) img.replaceWithData (image.data(), image.size()); else img.create();

    auto q = [] (const juce::File& f) { return "'" + f.getFullPathName().replace ("'", "'\\''") + "'"; };
    const juce::String shell = "/bin/sh " + q (script) + " " + (write ? "write " : "read ") + juce::String (id) + " "
                             + q (img) + " " + q (prog) + " " + q (abortFile) + " " + (verify ? "1" : "0") + " " + juce::String (limit);

    // Follows the progress file until the script reports OK / ERR.  `alive` tells whether the launching process still runs.
    auto follow = [&] (std::function<bool()> alive, int waitLimitMs) -> juce::String
    {
        juce::String last = "WAIT";
        const auto t0 = juce::Time::getMillisecondCounter();
        auto lastChange = t0;
        for (;;)
        {
            wait (80);
            const auto s = prog.loadFileAsString().trim();
            const auto now = juce::Time::getMillisecondCounter();
            if (s != last)
            {
                last = s; lastChange = now;
                if (s.startsWith ("W ") || s.startsWith ("V "))
                {
                    j.verifying = s[0] == 'V';
                    j.track = juce::jlimit (0, 159, s.substring (2).getIntValue());
                    if (! write && (j.sysLine.empty() || j.label.empty()))          // name and system text from the part already read
                    {
                        juce::FileInputStream in (juce::File (img.getFullPathName() + ".part"));
                        if (in.openedOk())
                        {
                            const auto size = in.getTotalLength();
                            uint8_t b[64] = {};
                            if (j.sysLine.empty() && size >= 0x40 && in.setPosition (0x20) && in.read (b, 32) == 32)
                            {
                                std::string t;
                                for (int k = 0; k < 31 && b[k] != 0xfe; ++k) t += (b[k] >= 32 && b[k] < 127) ? (char) b[k] : ' ';
                                while (! t.empty() && t.back() == ' ') t.pop_back();
                                j.sysLine = t;
                            }
                            if (j.label.empty() && size >= 0x10c40 && in.setPosition (0x10c33) && in.read (b, 12) == 12)
                            {
                                std::string t;
                                for (int k = 0; k < 12; ++k) t += (b[k] >= 32 && b[k] < 127) ? (char) b[k] : ' ';
                                j.label = t;
                            }
                        }
                    }
                    publish (j);
                }
            }
            if (s == "OK" || s.startsWith ("ERR ")) return s;
            if (threadShouldExit()) abortFile.create();
            if (! alive() && (s == "WAIT" || s.startsWith ("W ") || s.startsWith ("V "))) { wait (200); return prog.loadFileAsString().trim().isEmpty() ? juce::String ("WAIT") : prog.loadFileAsString().trim(); }
            if (s == "WAIT" && abortFile.exists()) return "ERR Cancelled";
            if (s == "WAIT" && now - t0 > (juce::uint32) waitLimitMs) return "ERR Cancelled (no password entered)";
            if (s != "WAIT" && now - lastChange > 60000) return "ERR No progress for 60 s";
        }
    };

    // 1st way: the system password dialog, asked ONCE per session: it starts the root helper, which then runs all jobs
    juce::String fin;
    juce::String osaOut;
    int osaCode = 0;
    auto helperAlive = [tmp] { const auto f = tmp.getChildFile ("alive"); return f.existsAsFile() && (juce::Time::getCurrentTime() - f.getLastModificationTime()).inSeconds() < 3.0; };
    if (! helperAlive())
    {
        auto hs = tmp.getChildFile ("helper.sh");
        hs.replaceWithText (kHelper, false, false, "\n");
        const juce::String start = "/bin/sh " + q (hs) + " " + q (tmp) + " " + juce::String ((int) ::getpid()) + " > /dev/null 2>&1 &";
        const juce::String osa = "do shell script \"" + start.replace ("\\", "\\\\").replace ("\"", "\\\"") + "\" with administrator privileges";
        juce::ChildProcess proc;
        if (! proc.start ({ "/usr/bin/osascript", "-e", osa })) fin = "ERR Cannot start osascript";
        else
        {
            const auto t0 = juce::Time::getMillisecondCounter();
            while (! helperAlive() && fin.isEmpty())
            {
                wait (100);
                if (threadShouldExit()) fin = "ERR Cancelled";
                else if (! proc.isRunning() && ! helperAlive())
                {
                    wait (400);
                    if (! helperAlive())
                    {
                        osaOut = proc.readAllProcessOutput().trim();
                        osaCode = (int) proc.getExitCode();
                        fin = (osaOut.contains ("-128") || osaOut.containsIgnoreCase ("cancel")) ? juce::String ("ERR Cancelled")
                                                                                              : "ERR Helper stopped (code " + juce::String (osaCode) + ") " + osaOut.substring (0, 240);
                    }
                }
                else if (juce::Time::getMillisecondCounter() - t0 > 120000) fin = "ERR Cancelled (no password entered)";
            }
        }
    }
    if (fin.isEmpty())
    {
        tmp.getChildFile ("job").replaceWithText (juce::String (write ? "write " : "read ") + juce::String (id) + " " + (verify ? "1" : "0") + " " + juce::String (limit) + "\n", false, false, "\n");
        fin = follow ([helperAlive] { return helperAlive(); }, 20000);
    }

    // 2nd way, only when macOS refused the raw access: a Terminal window (it has the right already), password typed there.
    if (fin.containsIgnoreCase ("not permitted") || osaOut.containsIgnoreCase ("not permitted"))
    {
        auto cmd = tmp.getChildFile ("S5 Floppy.command");
        cmd.replaceWithText (juce::String ("#!/bin/sh\n")
            + "echo 'S5 Sampler - USB floppy ' " + (write ? "'write'" : "'read'") + "\n"
            + "echo 'Enter your Mac password (nothing is shown while you type):'\n"
            + "sudo " + shell + "\n"
            + "rc=$?\n"
            + "if [ $rc -ne 0 ] && ! grep -q '^ERR' " + q (prog) + "; then echo \"ERR Script stopped (code $rc)\" > " + q (prog) + "; fi\n"
            + "echo\necho 'Finished - you can close this window (Cmd-W).'\n", false, false, "\n");
        { juce::ChildProcess c; c.start ({ "/bin/chmod", "755", cmd.getFullPathName() }); c.waitForProcessToFinish (2000); }
        prog.replaceWithText ("WAIT");
        abortFile.deleteFile();
        juce::File (img.getFullPathName() + ".part").deleteFile();
        juce::ChildProcess t;
        if (t.start ({ "/usr/bin/open", "-a", "Terminal", cmd.getFullPathName() }))
            fin = follow ([] { return true; }, 120000);
        else fin = "ERR Cannot start Terminal";
    }
    if (fin == "OK")
    {
        j.finished = true; j.ok = true; j.track = limit;
        if (! write)
        {
            juce::MemoryBlock mb;
            if (img.loadFileAsData (mb) && (int) mb.getSize() == data::kImageSize)
                j.data.assign ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
            else { j.ok = false; j.error = "Read incomplete"; }
            const auto bad = juce::File (img.getFullPathName() + ".bad");
            if (j.ok && bad.existsAsFile())
            {
                int sound = 0, system = 0;
                for (auto& l : juce::StringArray::fromLines (bad.loadFileAsString().trim()))
                    if (l.trim().isNotEmpty()) { if (l.getIntValue() >= 14) ++sound; else ++system; }
                if (sound > 0) j.error = juce::String (sound).toStdString() + " bad";
                else if (system > 0) j.error = "no system";
            }
        }
        publish (j);
    }
    else fail (fin.substring (4) + (fin.containsIgnoreCase ("not permitted") ? "  -> System Settings > Privacy & Security > Full Disk Access: allow this app and Terminal, restart them" : ""));

    for (auto* n : { "disk.img", "disk.img.part", "disk.img.bad", "disk.img.err", "disk.img.chunk", "disk.img.chunk.s", "progress.txt", "abort", "job", "S5 Floppy.command" })
        tmp.getChildFile (n).deleteFile();
    abortFile = juce::File();
    stateValue = (int) State::NoDisk;                 // rescan shows the real state
    ++serial;
}

bool MacFloppy::tick (int)
{
    const int s = serial.load();
    if (s == seenSerial) return false;
    seenSerial = s;
    std::lock_guard<std::mutex> g (lock);
    shown = pub;
    return true;
}
}
