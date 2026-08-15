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

#include <clap/clap.h>

#include "../plugin/Parameters.h"

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
        printf("  cannot open %s\n", path.getFullPathName().toRawUTF8());
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
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();

    printf("%s\n", failures ? "FAILED" : "all CLAP checks passed");
    return failures ? 1 : 0;
}
