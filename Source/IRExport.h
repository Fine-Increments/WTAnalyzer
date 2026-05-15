/*
  ==============================================================================

    IRExport.h
    Header-only helper for writing captured impulse responses to a 32-bit
    float stereo WAV. Used by both ImpulseDisplay (Direct Impulse IR) and
    FarinaDisplay (Farina log-sweep IR) so the export path is uniform across
    modes.

    Stereo WAV is the canonical convolution-reverb format; mono IRs are
    written into both channels so the file is drop-in compatible with hosts
    expecting stereo data.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>

namespace IRExport
{
    // Writes the captured IR to destination as a 32-bit float stereo WAV.
    // - sampleRate is the host sample rate at the time of capture.
    // - lengthL / lengthR are the per-channel valid sample counts; the
    //   longer one defines the file length and the shorter is zero-padded.
    // - If only one channel has data, that data is duplicated into both
    //   WAV channels (common case when sidechain is delivered mono).
    // - Overwrites destination if it already exists.
    // Returns true on success, false on any IO / format error.
    inline bool writeStereoWav (const juce::File& destination,
                                const std::vector<float>& bufL, int lengthL,
                                const std::vector<float>& bufR, int lengthR,
                                double sampleRate)
    {
        if (sampleRate <= 0.0) return false;

        const int totalLen = juce::jmax (lengthL, lengthR);
        if (totalLen <= 0) return false;

        destination.deleteFile();
        auto stream = destination.createOutputStream();
        if (stream == nullptr) return false;

        juce::WavAudioFormat wav;
        const juce::AudioFormatWriterOptions opts =
            juce::AudioFormatWriterOptions()
                .withSampleRate     (sampleRate)
                .withNumChannels    (2)
                .withBitsPerSample  (32)
                .withSampleFormat   (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), opts));
        if (writer == nullptr) return false;
        stream.release();   // writer takes ownership of the stream

        juce::AudioBuffer<float> outBuf (2, totalLen);
        outBuf.clear();

        auto fillFrom = [&] (int destChannel, const std::vector<float>& src, int srcLen)
        {
            const int n = juce::jmin (srcLen, totalLen);
            for (int i = 0; i < n; ++i)
                outBuf.setSample (destChannel, i, src[(size_t) i]);
        };

        const bool hasL = lengthL > 0 && ! bufL.empty();
        const bool hasR = lengthR > 0 && ! bufR.empty();

        if (hasL && hasR)
        {
            fillFrom (0, bufL, lengthL);
            fillFrom (1, bufR, lengthR);
        }
        else if (hasL)
        {
            // Mono IR - duplicate into both channels for compatibility.
            fillFrom (0, bufL, lengthL);
            fillFrom (1, bufL, lengthL);
        }
        else if (hasR)
        {
            fillFrom (0, bufR, lengthR);
            fillFrom (1, bufR, lengthR);
        }
        else
        {
            return false;
        }

        return writer->writeFromAudioSampleBuffer (outBuf, 0, totalLen);
    }
}
