#pragma once
// Image folder on the disk: ~/Documents/S5Sampler/Disks, one 737280 byte file per floppy image.
#include "../Screen/App.h"
#include "../Data/Hfe.h"
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace s5
{
class JuceFiles : public FileService
{
public:
    JuceFiles()
        : dir (juce::File::getSpecialLocation (juce::File::tempDirectory))
    {
    }

    bool dialogs() const override { return true; }
    std::string folderName() const override { return ""; }

    std::vector<std::string> list() override
    {
        return {};                                          // no fixed folder: files are chosen in the Finder
    }

    int sizeOf (const std::string& name) override
    {
        auto f = fileFor (name);
        return f.existsAsFile() ? (int) f.getSize() : -1;
    }

    bool read (const std::string& name, std::vector<uint8_t>& out) override
    {
        auto f = fileFor (name);
        if (! f.existsAsFile()) return false;
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb)) return false;
        out.assign ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
        return true;
    }

    bool write (const std::string& name, const std::vector<uint8_t>& data) override
    {
        auto f = fileFor (name);
        // Write to a temp file first so a failed write never destroys an existing image.
        juce::TemporaryFile tmp (f);
        if (! tmp.getFile().replaceWithData (data.data(), data.size())) return false;
        return tmp.overwriteTargetFileWithTemporary();
    }

    void pick (std::function<void (bool, const std::string&, std::vector<uint8_t>&)> done, const std::string& title = "Open disk image") override
    {
        chooser = std::make_unique<juce::FileChooser> (juce::String (title), lastDir, "*.img;*.IMG;*.out;*.OUT;*.hfe;*.HFE;*.ima;*.IMA");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, done] (const juce::FileChooser& fc)
                              {
                                  std::vector<uint8_t> none, raw;
                                  auto f = fc.getResult();
                                  if (f == juce::File()) { done (false, "Cancelled", none); return; }
                                  lastDir = f.getParentDirectory();
                                  juce::MemoryBlock mb;
                                  if (! f.loadFileAsData (mb)) { done (false, "Cannot read file", none); return; }
                                  std::vector<uint8_t> in ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
                                  if (! data::toRawImage (in, raw)) { done (false, "Not a disk image", none); return; }
                                  done (true, f.getFileNameWithoutExtension().toUpperCase().toStdString(), raw);
                              });
    }

    // Load Wave: audio files (WAV, AIFF, MP3, FLAC, Ogg, M4A where the system can decode them)
    void pickAudio (std::function<void (bool, const std::string&, std::vector<uint8_t>&)> done) override
    {
        chooser = std::make_unique<juce::FileChooser> ("Load Wave", lastDir, "*.wav;*.WAV;*.aif;*.AIF;*.aiff;*.AIFF;*.mp3;*.MP3;*.flac;*.FLAC;*.ogg;*.m4a");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, done] (const juce::FileChooser& fc)
                              {
                                  std::vector<uint8_t> none;
                                  auto f = fc.getResult();
                                  if (f == juce::File()) { done (false, "Cancelled", none); return; }
                                  lastDir = f.getParentDirectory();
                                  juce::MemoryBlock mb;
                                  if (! f.loadFileAsData (mb)) { done (false, "Cannot read file", none); return; }
                                  std::vector<uint8_t> in ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
                                  done (true, f.getFileName().toStdString(), in);
                              });
    }

    bool decodeAudio (const std::string& name, const std::vector<uint8_t>& bytes, std::vector<float>& mono, double& rate, std::string& err) override
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes.data(), bytes.size(), false)));
        if (! r) { err = "Cannot read " + juce::File (juce::String (name)).getFileExtension().toStdString() + " files"; return false; }
        const juce::int64 len = std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 20.0));   // 20 s are more than a wave bank holds
        if (len < 8 || r->numChannels < 1) { err = "Empty file"; return false; }
        juce::AudioBuffer<float> buf ((int) r->numChannels, (int) len);
        if (! r->read (&buf, 0, (int) len, 0, true, true)) { err = "Read error"; return false; }
        mono.assign ((size_t) len, 0.0f);
        for (int c = 0; c < buf.getNumChannels(); ++c)
            for (int i = 0; i < (int) len; ++i) mono[(size_t) i] += buf.getSample (c, i) / (float) buf.getNumChannels();
        rate = r->sampleRate;
        return true;
    }

    // Save dialog: the Finder's save dialog (NSSavePanel through JUCE) chooses folder and name of the .IMG
    void saveAs (const std::string& suggested, const std::vector<uint8_t>& data,
                 std::function<void (bool, const std::string&)> done) override
    {
        auto bytes = std::make_shared<std::vector<uint8_t>> (data);
        chooser = std::make_unique<juce::FileChooser> ("Save disk image",
                                                       lastDir.getChildFile (juce::String (suggested) + ".IMG"), "*.img");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, bytes, done] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) { done (false, "Cancelled"); return; }
                                  if (! f.hasFileExtension ("img")) f = f.withFileExtension ("img");
                                  lastDir = f.getParentDirectory();
                                  juce::TemporaryFile tmp (f);
                                  if (tmp.getFile().replaceWithData (bytes->data(), bytes->size()) && tmp.overwriteTargetFileWithTemporary())
                                      done (true, f.getFileName().toStdString());
                                  else
                                      done (false, "write error");
                              });
    }

    // Save in two steps: the dialog first (folder and name), the data later
    void chooseSave (const std::string& suggested, std::function<void (bool, const std::string&)> done) override
    {
        chooser = std::make_unique<juce::FileChooser> ("Save disk image",
                                                       lastDir.getChildFile (juce::String (suggested) + ".IMG"), "*.img");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, done] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) { done (false, "Cancelled"); return; }
                                  if (! f.hasFileExtension ("img")) f = f.withFileExtension ("img");
                                  lastDir = f.getParentDirectory();
                                  chosenFile = f;
                                  done (true, f.getFileName().toStdString());
                              });
    }
    bool writeChosen (const std::vector<uint8_t>& data, std::string& err) override
    {
        if (chosenFile == juce::File()) { err = "no file chosen"; return false; }
        juce::TemporaryFile tmp (chosenFile);
        if (tmp.getFile().replaceWithData (data.data(), data.size()) && tmp.overwriteTargetFileWithTemporary()) return true;
        err = "write error"; return false;
    }

private:
    juce::File chosenFile;
    juce::File fileFor (const std::string& name) const
    {
        juce::String n (name);
        n = n.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-. ").trim();
        if (n.isEmpty()) n = "UNTITLED";
        return dir.getChildFile (n + ".IMG");
    }

    juce::File dir;
    juce::File lastDir { juce::File::getSpecialLocation (juce::File::userDesktopDirectory) };
    std::unique_ptr<juce::FileChooser> chooser;
};
}
