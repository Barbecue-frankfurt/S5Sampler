#pragma once
// Real USB floppy drive access (macOS).  Reads and writes raw 720 KB images track by track.
// Raw device access needs administrator rights, so the work is done by a small shell script that is
// started once through "do shell script ... with administrator privileges" (the system password dialog).
// Safeguards: only external, 737280 byte media are touched, the disk id is validated, writing needs
// the confirmation shown by the screen, and an abort file stops the script between two tracks.
#include "../Screen/App.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <mutex>

namespace s5
{
class MacFloppy : public FloppyDrive, private juce::Thread
{
public:
    MacFloppy();
    ~MacFloppy() override;

    State state() override { return (State) stateValue.load(); }
    std::string name() override { return "USB Floppy"; }
    bool startRead() override { return start (false, {}, false, 160); }
    bool startRead (int tracks) override { return start (false, {}, false, tracks); }
    bool startWrite (const std::vector<uint8_t>& image, bool verify) override { return start (true, image, verify, 160); }
    void abort() override;
    const Job& job() override { return shown; }
    bool tick (int ms) override;

private:
    void run() override;
    bool start (bool write, const std::vector<uint8_t>& image, bool verify, int limit = 160);
    void scan();
    void runJob (bool write, std::vector<uint8_t> image, bool verify, int limit);
    void publish (const Job& j);

    std::atomic<int> stateValue { (int) State::NoDrive };
    std::atomic<int> serial { 0 };
    int seenSerial = 0;
    std::string diskId;                      // e.g. "disk4", valid while state is Ready
    std::mutex lock;
    Job pub, shown;                          // pub is written by the worker, shown is the copy the screen reads
    bool jobPending = false, jobWrite = false, jobVerify = false; int jobLimit = 160;
    std::vector<uint8_t> jobImage;
    juce::File abortFile;
    juce::File helperDir;                    // one private folder per session: the root helper (password asked once) works in it
};

}
