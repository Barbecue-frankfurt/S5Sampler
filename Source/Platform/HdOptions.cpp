#include "HdOptions.h"
#include "../Screen/App.h"
#include "../Data/HdImage.h"

namespace s5
{
namespace
{
juce::PropertiesFile* settings()
{
    static juce::ApplicationProperties props;
    static bool ready = false;
    if (! ready)
    {
        ready = true;
        juce::PropertiesFile::Options o;
        o.applicationName = "S5Sampler";
        o.filenameSuffix = ".hd.settings";
        o.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        o.folderName = "~/.config";
       #else
        o.folderName = "";
       #endif
        props.setStorageParameters (o);
    }
    return props.getUserSettings();
}

void saveSettings()
{
    auto& a = App::get();
    if (auto* f = settings())
    {
        for (int i = 0; i < 7; ++i) f->setValue ("hd" + juce::String (i), juce::String (a.hdDev[i].ok ? a.hdDev[i].path : std::string()));
        f->setValue ("cd", a.cdId);
        f->saveIfNeeded();
    }
}

bool writeRangeImpl (const std::string& path, size_t offset, const uint8_t* data, size_t n)
{
    juce::File f (juce::String::fromUTF8 (path.c_str()));
    if (! f.existsAsFile()) return false;
    juce::FileOutputStream fos (f);
    if (fos.failedToOpen() || ! fos.setPosition ((juce::int64) offset)) return false;
    if (! fos.write (data, n)) return false; fos.flush(); return true;
}

bool readPathImpl (const std::string& path, std::vector<uint8_t>& out)
{
    juce::File f (juce::String::fromUTF8 (path.c_str()));
    juce::MemoryBlock mb;
    if (! f.existsAsFile() || ! f.loadFileAsData (mb)) return false;
    out.assign ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
    return true;
}

bool writeBlankImage (const juce::File& f, int mb, std::string& err)
{
    const auto v = data::HdImage::makeBlank (mb);
    if (f.existsAsFile() && ! f.deleteFile()) { err = "Cannot replace the file"; return false; }
    juce::FileOutputStream fos (f);
    if (fos.failedToOpen()) { err = "Cannot create the file"; return false; }
    constexpr size_t chunk = 1 << 20;
    for (size_t off = 0; off < v.size(); off += chunk)
        if (! fos.write (v.data() + off, std::min (chunk, v.size() - off))) { err = "Disk full?"; return false; }
    fos.flush();
    return true;
}

std::unique_ptr<juce::FileChooser> createChooser;

// ---------------------------------------------------------------------------------------------------------------
class HdOptionsComponent : public juce::Component
{
public:
    HdOptionsComponent()
    {
        for (int i = 0; i < 7; ++i)
        {
            auto& r = rows[i];
            r.id.setText ("ID " + juce::String (i), juce::dontSendNotification);
            r.name.setJustificationType (juce::Justification::centredLeft);
            r.choose.setButtonText ("Choose ...");
            r.remove.setButtonText ("Remove");
            r.choose.onClick = [this, i] { choose (i); };
            r.remove.onClick = [this, i] { App::get().hdDetach (i); refresh(); };
            addAndMakeVisible (r.id); addAndMakeVisible (r.name); addAndMakeVisible (r.choose); addAndMakeVisible (r.remove);
        }
        id7.setText ("ID 7   S-550 (fixed)", juce::dontSendNotification);
        id7.setColour (juce::Label::textColourId, juce::Colours::grey);
        cdLabel.setText ("CD-5 CD-ROM", juce::dontSendNotification);
        cd.addItem ("not connected", 1);
        for (int i = 0; i < 7; ++i) cd.addItem ("ID " + juce::String (i), 2 + i);
        cd.onChange = [this]
        {
            std::string err;
            if (! App::get().setCdId (cd.getSelectedId() - 2, err)) status.setText (err, juce::dontSendNotification);
            refresh();
        };
        note.setText ("The S-550 works with the hard disk of the highest ID. Each ID needs its own number; the CD-5 and every hard disk must differ.",
                      juce::dontSendNotification);
        note.setFont (juce::Font (juce::FontOptions (12.0f)));
        note.setColour (juce::Label::textColourId, juce::Colours::grey);
        status.setColour (juce::Label::textColourId, juce::Colours::orange);
        createBtn.setButtonText ("Create Hard disk Image ...");
        createBtn.onClick = [] { showCreateHdImage(); };
        for (auto* c : std::initializer_list<juce::Component*> { &id7, &cdLabel, &cd, &note, &status, &createBtn }) addAndMakeVisible (c);
        setSize (640, 36 + 7 * 32 + 30 + 40 + 56 + 24 + 40);
        refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 12);
        for (int i = 0; i < 7; ++i)
        {
            auto line = r.removeFromTop (32);
            rows[i].id.setBounds (line.removeFromLeft (52));
            rows[i].remove.setBounds (line.removeFromRight (80).reduced (2));
            rows[i].choose.setBounds (line.removeFromRight (100).reduced (2));
            rows[i].name.setBounds (line);
        }
        id7.setBounds (r.removeFromTop (30));
        r.removeFromTop (10);
        auto l = r.removeFromTop (30);
        cdLabel.setBounds (l.removeFromLeft (120));
        cd.setBounds (l.removeFromLeft (160));
        r.removeFromTop (6);
        note.setBounds (r.removeFromTop (34));
        status.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        createBtn.setBounds (r.removeFromTop (30).removeFromLeft (220));
    }

private:
    struct Row { juce::Label id, name; juce::TextButton choose, remove; };

    void choose (int i)
    {
        chooser = std::make_unique<juce::FileChooser> ("Hard disk image for ID " + juce::String (i), lastDir, "*");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, i, alive = juce::Component::SafePointer<juce::Component> (this)] (const juce::FileChooser& fc)
                              {
                                  if (alive == nullptr) return;
                                  const auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  lastDir = f.getParentDirectory();
                                  std::string err;
                                  if (! App::get().hdAttach (i, f.getFullPathName().toStdString(), err)) status.setText (err, juce::dontSendNotification);
                                  else status.setText ("", juce::dontSendNotification);
                                  refresh();
                              });
    }

    void refresh()
    {
        auto& a = App::get();
        for (int i = 0; i < 7; ++i)
        {
            const auto& d = a.hdDev[i];
            const bool used = i == a.cdId;
            rows[i].name.setText (d.ok ? juce::String (d.name) + ".IMG   (" + juce::String (d.areas) + " areas)" + (i == a.hdActiveId ? "   - active" : "")
                                       : (used ? juce::String ("CD-5") : juce::String ("-- no hard disk --")), juce::dontSendNotification);
            rows[i].name.setColour (juce::Label::textColourId, d.ok ? juce::Colours::white : juce::Colours::grey);
            rows[i].choose.setEnabled (! used);
            rows[i].remove.setEnabled (d.ok);
        }
        cd.setSelectedId (a.cdId < 0 ? 1 : 2 + a.cdId, juce::dontSendNotification);
        for (int i = 0; i < 7; ++i) cd.setItemEnabled (2 + i, ! a.hdDev[i].ok);
    }

    Row rows[7];
    juce::Label id7, cdLabel, note, status;
    juce::TextButton createBtn;
    juce::ComboBox cd;
    juce::File lastDir { juce::File::getSpecialLocation (juce::File::userDesktopDirectory) };
    std::unique_ptr<juce::FileChooser> chooser;
};
}

void initHdSettings()
{
    auto& a = App::get();
    a.readPath = readPathImpl;
    a.writeRange = writeRangeImpl;
    a.hdConfigChanged = saveSettings;
    if (auto* f = settings())
    {
        std::string err;
        const int cdv = f->getIntValue ("cd", -1);
        for (int i = 0; i < 7; ++i)
        {
            const auto p = f->getValue ("hd" + juce::String (i));
            if (p.isNotEmpty()) a.hdAttach (i, p.toStdString(), err);
        }
        if (cdv >= 0 && cdv <= 6) a.setCdId (cdv, err);
    }
}

int storedDisplayScale() { if (auto* f = settings()) return juce::jlimit (1, 2, f->getIntValue ("scale", 2)); return 2; }
void storeDisplayScale (int v) { if (auto* f = settings()) { f->setValue ("scale", v); f->saveIfNeeded(); } }

void showHdOptions()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new HdOptionsComponent());
    o.dialogTitle = "Hard disk images (SCSI)";
    o.dialogBackgroundColour = juce::Colour (0xff202428);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void showCreateHdImage()
{
    auto* w = new juce::AlertWindow ("Create Hard disk Image",
                                     "A new, formatted image (empty areas H11 - H88). It can be used at once: Load, Save and Setup work with it.",
                                     juce::MessageBoxIconType::NoIcon);
    w->addComboBox ("size", { "20 MB   (16 areas)", "40 MB   (32 areas)", "60 MB   (48 areas)", "80 MB   (64 areas)" }, "Size");
    juce::StringArray ids { "do not attach" };
    for (int i = 0; i < 7; ++i) ids.add ("SCSI ID " + juce::String (i));
    w->addComboBox ("id", ids, "Attach to");
    w->getComboBoxComponent ("size")->setSelectedItemIndex (3);
    w->addButton ("Create ...", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w] (int r)
    {
        if (r != 1) return;
        const int mb = 20 * (w->getComboBoxComponent ("size")->getSelectedItemIndex() + 1);
        const int id = w->getComboBoxComponent ("id")->getSelectedItemIndex() - 1;
        createChooser = std::make_unique<juce::FileChooser> ("Save the new hard disk image",
                            juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("HardDisk_" + juce::String (mb) + "MB.img"), "*.img");
        createChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                    [mb, id] (const juce::FileChooser& fc)
                                    {
                                        auto f = fc.getResult();
                                        if (f == juce::File()) return;
                                        if (f.getFileExtension().isEmpty()) f = f.withFileExtension ("img");
                                        std::string err;
                                        juce::String msg;
                                        if (! writeBlankImage (f, mb, err)) msg = "Could not create the image: " + juce::String (err);
                                        else
                                        {
                                            msg = juce::String (mb) + " MB image created:\n" + f.getFullPathName();
                                            if (id >= 0)
                                            {
                                                if (App::get().hdAttach (id, f.getFullPathName().toStdString(), err)) msg += "\n\nAttached to SCSI ID " + juce::String (id) + ".";
                                                else msg += "\n\nNot attached: " + juce::String (err);
                                            }
                                        }
                                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Create Hard disk Image", msg);
                                    });
    }), true);
}
}
