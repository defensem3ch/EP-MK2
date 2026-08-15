// Render the editor to a PNG, for the README.
//
// A separate tool rather than a by-product of the test suite: the test writes
// whatever size it happened to be exercising, which is not what should end up
// in the documentation.  This renders at the design size, so the image is
// pixel-for-pixel what the panel was laid out to be rather than a resampling
// of it.
//
//   epmk2-screenshot docs/screenshot.png [--param <id> <value>]...
#include <cstdio>
#include <cstring>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../plugin/PluginEditor.h"
#include "../plugin/PluginProcessor.h"

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: %s <out.png> [--param <id> <value>]...\n", argv[0]);
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    EpMk2Processor proc;
    proc.setPlayConfigDetails(0, 2, 48000.0, 512);
    proc.prepareToPlay(48000.0, 512);

    for (int i = 2; i + 2 < argc; ++i)
        if (! strcmp(argv[i], "--param")) {
            if (auto* p = proc.getState().getParameter(argv[i + 1]))
                p->setValueNotifyingHost(p->convertTo0to1((float) atof(argv[i + 2])));
            i += 2;
        }

    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    if (ed == nullptr) {
        printf("no editor\n");
        return 1;
    }

    // The design size, not whatever this machine last left in its settings.
    if (auto* e = dynamic_cast<EpMk2Editor*>(ed.get()))
        ed->setSize(EpMk2Editor::kDesignWidth, e->designHeightForTest());

    // The greying of dependent controls is applied on a timer that will never
    // fire here, and once at construction -- which is why it is done there.
    const juce::Image img = ed->createComponentSnapshot(ed->getLocalBounds());
    if (! img.isValid()) {
        printf("snapshot failed\n");
        return 1;
    }

    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));
    out.deleteFile();
    if (auto stream = out.createOutputStream()) {
        juce::PNGImageFormat png;
        if (! png.writeImageToStream(img, *stream)) {
            printf("could not write %s\n", out.getFullPathName().toRawUTF8());
            return 1;
        }
    }

    printf("wrote %s (%d x %d)\n", out.getFullPathName().toRawUTF8(),
           img.getWidth(), img.getHeight());
    return 0;
}
