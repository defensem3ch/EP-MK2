// Load the built .clap the way a host would, and ask it what it is.
//
// "The file was produced" and "a host can load it" are different claims, and
// only the second one matters.  This dlopens the binary, walks the factory,
// instantiates the plugin and reads its parameter list back -- which is the
// first few things any CLAP host does, and the ones that fail loudly if the
// wrapper is wired up wrong.
//
//   ./epmk2-claptest path/to/EP-MK2.clap
#include <cstdio>
#include <cstring>

#include <juce_core/juce_core.h>

#if ! JUCE_WINDOWS
 #include <dlfcn.h>          // for dlerror only: the load itself is JUCE's
#endif

#include <clap/clap.h>

#include "../plugin/Parameters.h"
#include "../plugin/Presets.h"

static int failures = 0;

static void check(bool ok, const char* what, const char* extra = "")
{
    printf("  %-46s %s%s\n", what, ok ? "ok" : "FAILED", extra);
    if (! ok) ++failures;
}

// The least a host can be: CLAP requires the struct, not that it does much.
static const void* hostExtension(const clap_host_t*, const char*) { return nullptr; }
static void hostNothing(const clap_host_t*) {}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: %s <plugin.clap>\n", argv[0]);
        return 1;
    }

    // juce::DynamicLibrary rather than dlopen, so this runs on the platforms
    // that most need checking: dlfcn.h does not exist on Windows, and a test
    // that cannot be built there is no test at all.
    juce::File path(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));

    // On macOS a .clap is a bundle -- a directory -- and what has to be
    // loaded is the binary inside it.
    if (path.isDirectory()) {
        const auto inner = path.getChildFile("Contents/MacOS")
                               .getChildFile(path.getFileNameWithoutExtension());
        if (inner.existsAsFile())
            path = inner;
    }

    juce::DynamicLibrary library;
    if (! library.open(path.getFullPathName())) {
        // The reason matters more than the fact.  An unresolved symbol and a
        // missing file both read as "cannot open", and the first one cost an
        // hour once.
        printf("  cannot open %s\n", path.getFullPathName().toRawUTF8());
       #if ! JUCE_WINDOWS
        if (const char* why = dlerror())
            printf("  %s\n", why);
       #endif
        return 1;
    }

    auto* entry = (const clap_plugin_entry_t*) library.getFunction("clap_entry");
    check(entry != nullptr, "the binary exports clap_entry");
    if (entry == nullptr)
        return 1;

    check(clap_version_is_compatible(entry->clap_version),
          "its CLAP version is one a host would accept");
    check(entry->init(path.getFullPathName().toRawUTF8()), "it initialises");

    auto* factory = (const clap_plugin_factory_t*)
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    check(factory != nullptr, "it offers a plugin factory");
    if (factory == nullptr)
        return 1;

    const uint32_t count = factory->get_plugin_count(factory);
    char cb[64];
    snprintf(cb, sizeof cb, "  (%u)", count);
    check(count == 1, "it contains exactly one plugin", cb);

    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
    check(desc != nullptr, "it describes itself");
    if (desc == nullptr)
        return 1;

    char idb[192];
    snprintf(idb, sizeof idb, "  (%s, \"%s\" by %s)", desc->id, desc->name, desc->vendor);
    // The id is the plugin's identity to a host: a project that saved a
    // reference to it will not find it again if this ever changes.
    check(std::strcmp(desc->id, "audio.defensem3ch.epmk2") == 0
              && std::strcmp(desc->name, "EP-MK2") == 0,
          "its id and name are what they should be", idb);

    clap_host_t host {};
    host.clap_version = CLAP_VERSION;
    host.name = "epmk2-claptest";
    host.vendor = "defensem3ch";
    host.url = "";
    host.version = "1";
    host.get_extension = hostExtension;
    host.request_restart = hostNothing;
    host.request_process = hostNothing;
    host.request_callback = hostNothing;

    const clap_plugin_t* plugin = factory->create_plugin(factory, &host, desc->id);
    check(plugin != nullptr, "a host can instantiate it");
    if (plugin == nullptr)
        return 1;

    check(plugin->init(plugin), "it initialises as a plugin");

    auto* params = (const clap_plugin_params_t*)
        plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    check(params != nullptr, "it exposes the params extension");

    if (params != nullptr) {
        const uint32_t n = params->count(plugin);
        const uint32_t expected = (uint32_t) epmk2::params::table().size();
        char pb[96];
        snprintf(pb, sizeof pb, "  (%u, and the table has %u)", n, expected);
        check(n == expected, "it publishes every parameter", pb);

        // Not just a count: a wrapper that reported the right number of
        // nameless parameters would pass the line above.
        uint32_t named = 0;
        for (uint32_t i = 0; i < n; ++i) {
            clap_param_info_t info {};
            if (params->get_info(plugin, i, &info) && info.name[0] != '\0')
                ++named;
        }
        char nb[96];
        snprintf(nb, sizeof nb, "  (%u of %u)", named, n);
        check(named == n, "and every one of them is named", nb);
    }

    check(plugin->activate(plugin, 48000.0, 1, 512), "it activates at 48 kHz");

    // The factory presets, which a CLAP host can only see through the preset
    // discovery factory -- there is no equivalent of a VST3 program list, and
    // without this they simply do not exist in a CLAP host.
    auto* discovery = (const clap_preset_discovery_factory_t*)
        entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID);
    check(discovery != nullptr, "it offers presets to browse");

    if (discovery != nullptr) {
        const auto* desc = discovery->get_descriptor(discovery, 0);
        check(discovery->count(discovery) == 1 && desc != nullptr,
              "one preset provider, with a descriptor");

        // Walk it the way a host does: create the provider, let it declare
        // where its presets are, then ask that location what is in it.
        static int declared = 0;
        static juce::StringArray found;
        declared = 0;
        found.clear();

        clap_preset_discovery_indexer_t indexer {};
        indexer.clap_version = CLAP_VERSION;
        indexer.name = "epmk2-claptest";
        indexer.vendor = "defensem3ch";
        indexer.url = "";
        indexer.version = "1";
        indexer.declare_filetype = [](const clap_preset_discovery_indexer*,
                                      const clap_preset_discovery_filetype*) { return true; };
        indexer.declare_location = [](const clap_preset_discovery_indexer*,
                                      const clap_preset_discovery_location_t* l) {
            // The spec is strict here: for presets inside the binary the
            // location must be null, and a host that sees a path will go
            // looking on disk and find nothing.
            if (l->kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && l->location == nullptr)
                ++declared;
            return true;
        };
        indexer.declare_soundpack = [](const clap_preset_discovery_indexer*,
                                       const clap_preset_discovery_soundpack*) { return true; };
        indexer.get_extension = [](const clap_preset_discovery_indexer*, const char*)
            -> const void* { return nullptr; };

        const auto* provider = discovery->create(discovery, &indexer, desc->id);
        check(provider != nullptr, "the provider can be created");

        if (provider != nullptr) {
            check(provider->init(provider) && declared == 1,
                  "it declares one location, inside the plugin");

            clap_preset_discovery_metadata_receiver_t receiver {};
            receiver.begin_preset = [](const clap_preset_discovery_metadata_receiver*,
                                       const char* name, const char*) {
                found.add(name);
                return true;
            };
            receiver.on_error = [](const clap_preset_discovery_metadata_receiver*,
                                   int32_t, const char*) {};
            receiver.add_plugin_id = [](const clap_preset_discovery_metadata_receiver*,
                                        const clap_universal_plugin_id_t*) {};
            receiver.add_feature = [](const clap_preset_discovery_metadata_receiver*,
                                      const char*) {};
            receiver.set_flags = [](const clap_preset_discovery_metadata_receiver*, uint32_t) {};
            receiver.set_timestamps = [](const clap_preset_discovery_metadata_receiver*,
                                         clap_timestamp, clap_timestamp) {};
            receiver.add_creator = [](const clap_preset_discovery_metadata_receiver*,
                                      const char*) {};
            receiver.set_description = [](const clap_preset_discovery_metadata_receiver*,
                                          const char*) {};
            receiver.set_soundpack_id = [](const clap_preset_discovery_metadata_receiver*,
                                           const char*) {};
            receiver.add_extra_info = [](const clap_preset_discovery_metadata_receiver*,
                                         const char*, const char*) {};

            provider->get_metadata(provider, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                   nullptr, &receiver);
            char pb[256];
            snprintf(pb, sizeof pb, "  (%d: %s)", found.size(),
                     found.joinIntoString(", ").toRawUTF8());
            check(found.size() == (int) epmk2::presets::table().size(),
                  "and every factory preset is offered", pb);

            provider->destroy(provider);
        }
    }
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();

    printf("%s\n", failures ? "FAILED" : "all CLAP checks passed");
    return failures ? 1 : 0;
}
