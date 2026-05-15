/*
  ==============================================================================

    SidecarReader.cpp

  ==============================================================================
*/

#include "SidecarReader.h"

juce::String ScriptContext::paramString (const juce::String& key,
                                         const juce::String& fallback) const
{
    if (parameters == nullptr || ! parameters->hasProperty (key)) return fallback;
    return parameters->getProperty (key).toString();
}

int ScriptContext::paramInt (const juce::String& key, int fallback) const
{
    if (parameters == nullptr || ! parameters->hasProperty (key)) return fallback;
    return (int) parameters->getProperty (key);
}

double ScriptContext::paramDouble (const juce::String& key, double fallback) const
{
    if (parameters == nullptr || ! parameters->hasProperty (key)) return fallback;
    return (double) parameters->getProperty (key);
}

bool ScriptContext::paramBool (const juce::String& key, bool fallback) const
{
    if (parameters == nullptr || ! parameters->hasProperty (key)) return fallback;
    return (bool) parameters->getProperty (key);
}

bool ScriptContext::hasParam (const juce::String& key) const
{
    return parameters != nullptr && parameters->hasProperty (key);
}

juce::String ScriptContext::shortSummary() const
{
    if (! valid) return {};

    // Format: "script.py - param1=value1, param2=value2, ..."
    // Bounded to ~80 chars so it stays in one caption line.
    juce::String head = scriptName;
    if (analysisHint.isNotEmpty())
        head << " (" << analysisHint << ")";

    juce::String paramList;
    if (parameters != nullptr)
    {
        auto& props = parameters->getProperties();
        int  count = 0;
        for (auto it = props.begin(); it != props.end(); ++it)
        {
            if (count > 0) paramList << ", ";
            paramList << it->name.toString() << "=" << it->value.toString();
            ++count;
            if (paramList.length() > 64) { paramList << " ..."; break; }
        }
    }

    return paramList.isEmpty() ? head : head + "  -  " + paramList;
}

SidecarReader::SidecarReader() = default;

SidecarReader::~SidecarReader()
{
    stopTimer();
}

void SidecarReader::setPath (const juce::File& file)
{
    jsonFile = file;

    if (! file.existsAsFile())
    {
        clear();
        return;
    }

    if (tryParse (file))
    {
        lastLoadedMtime = file.getLastModificationTime();
        ++revision;
        if (onContextChanged) onContextChanged();
    }

    startTimerHz (kPollHz);
}

void SidecarReader::clear()
{
    stopTimer();
    jsonFile        = juce::File();
    context         = ScriptContext{};
    lastLoadedMtime = juce::Time{};
    ++revision;
    if (onContextChanged) onContextChanged();
}

void SidecarReader::timerCallback()
{
    reloadIfChanged();
}

void SidecarReader::reloadIfChanged()
{
    if (! jsonFile.existsAsFile())
    {
        if (context.valid)
        {
            context = ScriptContext{};
            ++revision;
            if (onContextChanged) onContextChanged();
        }
        return;
    }

    const auto mtime = jsonFile.getLastModificationTime();
    if (mtime == lastLoadedMtime) return;

    if (tryParse (jsonFile))
    {
        lastLoadedMtime = mtime;
        ++revision;
        if (onContextChanged) onContextChanged();
    }
}

bool SidecarReader::tryParse (const juce::File& file)
{
    auto json = juce::JSON::parse (file);
    if (! json.isObject())
    {
        context = ScriptContext{};
        return true;   // "parsed" as an invalid context; revision still bumps
    }

    auto* obj = json.getDynamicObject();
    if (obj == nullptr) return false;

    ScriptContext c;
    c.sourceFile     = file;
    c.scriptName     = obj->getProperty ("script")      .toString();
    c.analysisHint   = obj->getProperty ("analysis")    .toString();
    c.generatedAt    = obj->getProperty ("generated_at").toString();

    if (obj->hasProperty ("audio"))
    {
        auto audioVar = obj->getProperty ("audio");
        if (auto* audio = audioVar.getDynamicObject())
        {
            c.sampleRate      = (int) audio->getProperty ("sample_rate");
            c.samplesPerFrame = (int) audio->getProperty ("samples_per_frame");
            c.frameCount      = (int) audio->getProperty ("frame_count");
            c.channels        = audio->hasProperty ("channels")
                                  ? (int) audio->getProperty ("channels")
                                  : 1;
        }
    }

    if (obj->hasProperty ("parameters"))
    {
        auto paramsVar = obj->getProperty ("parameters");
        if (auto* params = paramsVar.getDynamicObject())
            c.parameters = juce::DynamicObject::Ptr (params);
    }

    // A context is considered valid if we got at least the script name
    // and an audio block with non-zero sample rate. The schema permits
    // empty parameters (for non-parameterised generators).
    c.valid = c.scriptName.isNotEmpty() && c.sampleRate > 0;
    context = c;
    return true;
}
