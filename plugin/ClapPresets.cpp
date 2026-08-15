// The factory presets, offered to a CLAP host.
//
// CLAP has no equivalent of a VST3 program list, and clap-juce-extensions does
// not map JUCE's programs to anything -- so in a CLAP host the seven factory
// presets simply did not exist.  A host browses presets through the preset
// discovery factory instead, which normally means files on disk.
//
// It does not have to.  CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN means "the
// presets are inside the binary and the plugin is the container", which is
// exactly what these are: a table in Presets.h, not files anyone should have
// to install, back up or keep in step with the code that reads them.
//
// clap-juce-extensions leaves a documented hole for this --
// clapJuceExtensionCustomFactory, enabled by CLAP_SUPPORTS_CUSTOM_FACTORY --
// so none of it needs the submodule to be forked.
#include <cstring>

#include <clap/clap.h>

#include "Presets.h"

namespace {

// The load_key a host hands back to presetLoadFromLocation is the preset's
// name.  It could be an index, but an index is a promise that the order will
// never change, and a project saved against index 4 would silently load a
// different sound the day one is inserted.
const char* kProviderId = "audio.defensem3ch.epmk2.presets";

// Which plugin these presets are for.  A named object rather than a compound
// literal: taking the address of a temporary is not something to hand across
// an ABI boundary, whatever the compiler allows.
const clap_universal_plugin_id_t kPluginId = { "clap", "audio.defensem3ch.epmk2" };

bool providerInit(const clap_preset_discovery_provider* provider);
void providerDestroy(const clap_preset_discovery_provider*) {}

bool providerGetMetadata(const clap_preset_discovery_provider* provider,
                         uint32_t locationKind, const char* location,
                         const clap_preset_discovery_metadata_receiver_t* receiver)
{
    juce::ignoreUnused(provider);
    if (locationKind != CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN || location != nullptr)
        return false;

    for (const auto& preset : epmk2::presets::table()) {
        if (! receiver->begin_preset(receiver, preset.name, preset.name))
            return true;      // the host has had enough
        receiver->add_plugin_id(receiver, &kPluginId);
        receiver->add_feature(receiver, CLAP_PLUGIN_FEATURE_INSTRUMENT);
        receiver->add_feature(receiver, "electric piano");
    }
    return true;
}

const void* providerGetExtension(const clap_preset_discovery_provider*, const char*)
{
    return nullptr;
}

const clap_preset_discovery_provider_descriptor_t kProviderDescriptor = {
    CLAP_VERSION_INIT,
    kProviderId,
    "EP-MK2 factory presets",
    "defensem3ch",
};

const clap_preset_discovery_indexer_t* gIndexer = nullptr;

const clap_preset_discovery_provider_t kProvider = {
    &kProviderDescriptor,
    nullptr,
    providerInit,
    providerDestroy,
    providerGetMetadata,
    providerGetExtension,
};

bool providerInit(const clap_preset_discovery_provider* provider)
{
    juce::ignoreUnused(provider);
    if (gIndexer == nullptr)
        return false;

    // One location, inside the plugin.  The spec requires location to be null
    // for this kind -- a path here is how a host decides we meant FILE and
    // then finds nothing at it.
    const clap_preset_discovery_location_t location = {
        CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT,
        "EP-MK2 factory presets",
        CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
        nullptr,
    };
    return gIndexer->declare_location(gIndexer, &location);
}

uint32_t factoryCount(const clap_preset_discovery_factory*) { return 1; }

const clap_preset_discovery_provider_descriptor_t*
factoryGetDescriptor(const clap_preset_discovery_factory*, uint32_t index)
{
    return index == 0 ? &kProviderDescriptor : nullptr;
}

const clap_preset_discovery_provider_t*
factoryCreate(const clap_preset_discovery_factory*,
              const clap_preset_discovery_indexer_t* indexer, const char* providerId)
{
    if (indexer == nullptr || providerId == nullptr
        || std::strcmp(providerId, kProviderId) != 0)
        return nullptr;
    gIndexer = indexer;
    return &kProvider;
}

const clap_preset_discovery_factory_t kFactory = {
    factoryCount,
    factoryGetDescriptor,
    factoryCreate,
};

} // namespace

// C++ linkage, not extern "C": the wrapper calls
// ::clapJuceExtensionCustomFactory as a C++ function, so an extern "C"
// definition here produces an unmangled symbol that never matches the mangled
// one it looks for -- and the failure is a .clap that will not load at all,
// with no clue as to why beyond an undefined symbol.
const void* clapJuceExtensionCustomFactory(const char* factoryId)
{
    if (factoryId == nullptr)
        return nullptr;
    // Both ids: hosts written against the draft still ask for that one, and
    // the header says the two are compatible.
    if (std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID) == 0
        || std::strcmp(factoryId, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT) == 0)
        return &kFactory;
    return nullptr;
}
