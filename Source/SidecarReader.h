/*
  ==============================================================================

    SidecarReader.h
    Reads the sidecar JSON emitted by WTGenerator alongside a generated
    test signal (PLANNING.md section 2.5). The sidecar captures the exact
    parameter values used to generate the most recent signal, so
    parameter-dependent analyses (Farina, multisine flatness, transfer
    function from noise, ...) can pull their configuration from the
    source-of-truth file rather than asking the user to re-enter values
    in the analyzer UI.

    Lifecycle:
      - User points the reader at a JSON file via setPath().
      - A polling Timer checks the file's modification time at a low rate
        (4 Hz) so re-runs of the source script are picked up automatically.
      - When the parsed context changes, listeners (the editor, future
        analyses) consume getContext() and getRevision().

    Thread model: lives on the message thread. Other components snapshot
    the context at controlled moments (mode change, capture trigger).
    Audio-thread code is NOT expected to poll this reader directly.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

struct ScriptContext
{
    bool         valid = false;

    juce::String scriptName;       // e.g., "chirp.py"
    juce::String analysisHint;     // e.g., "FarinaDeconvolution"
    juce::String generatedAt;      // ISO 8601 timestamp string
    juce::File   sourceFile;       // the sidecar JSON itself

    int          sampleRate     = 0;
    int          samplesPerFrame = 0;
    int          frameCount     = 0;
    int          channels       = 1;

    // Parameter values. Keys match the <param name="..."> from the
    // script's .xml; values are typed per the schema (string for
    // Choice, int for Int, float for Float, bool for Bool).
    juce::DynamicObject::Ptr parameters;

    juce::String paramString (const juce::String& key, const juce::String& fallback = {}) const;
    int          paramInt    (const juce::String& key, int   fallback = 0) const;
    double       paramDouble (const juce::String& key, double fallback = 0.0) const;
    bool         paramBool   (const juce::String& key, bool  fallback = false) const;
    bool         hasParam    (const juce::String& key) const;

    // A short, human-readable single-line summary of the loaded context,
    // suitable for embedding in the per-analysis caption row in the editor.
    // Empty string when valid is false.
    juce::String shortSummary() const;
};

class SidecarReader  : private juce::Timer
{
public:
    SidecarReader();
    ~SidecarReader() override;

    // Set the JSON path to watch. nullptr / empty resets to the no-sidecar
    // state. The reader re-parses immediately on path change, then polls
    // mtime at 4 Hz to catch script re-runs.
    void setPath (const juce::File& jsonFile);
    void clear();

    const ScriptContext& getContext()  const noexcept { return context; }
    int                  getRevision() const noexcept { return revision; }

    // Optional change notification for any consumer that wants to react
    // immediately (e.g., the editor updating its caption). Stored as a
    // std::function so the editor can capture by lambda.
    std::function<void()> onContextChanged;

private:
    void timerCallback() override;
    void reloadIfChanged();
    bool tryParse (const juce::File& file);

    juce::File         jsonFile;
    juce::Time         lastLoadedMtime;
    ScriptContext      context;
    int                revision = 0;

    static constexpr int kPollHz = 4;
};
