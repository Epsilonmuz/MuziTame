#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// macOS merges a window's dirty areas into one rectangle, so when the analyser (top) and the
// meters (bottom right) update, CoreGraphics is asked to redraw everything in between, every
// knob included, 30 times a second. Attached to the editor, this cache keeps a bitmap of the
// whole window. JUCE's software renderer re-draws only the areas that really changed, each on
// its own, and the system shows the result as one image.
class SoftwareCachedImage : public juce::CachedComponentImage
{
public:
    explicit SoftwareCachedImage (juce::Component& c) : owner (c) {}
    void paint (juce::Graphics& g) override
    {
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const auto bounds = owner.getLocalBounds();
        const auto imageBounds = (bounds.toFloat() * scale).getSmallestIntegerContainer();
        if (image.isNull() || image.getWidth() != imageBounds.getWidth() || image.getHeight() != imageBounds.getHeight())
        {
            image = juce::Image (juce::Image::RGB, juce::jmax (1, imageBounds.getWidth()), juce::jmax (1, imageBounds.getHeight()), true, juce::NativeImageType());
            valid.clear();
        }
        if (! valid.containsRectangle (bounds))
        {
            juce::RectangleList<int> dirty (bounds);
            for (auto& r : valid) dirty.subtract (r);
            dirty.consolidate();
            juce::LowLevelGraphicsSoftwareRenderer renderer (image);
            renderer.addTransform (juce::AffineTransform::scale (scale));
            juce::Graphics ig (renderer);
            for (auto& r : dirty)       // one at a time: JUCE picks the children to paint from the clip's bounding box
            {
                juce::Graphics::ScopedSaveState state (ig);
                ig.reduceClipRegion (r);
                owner.paintEntireComponent (ig, true);
            }
            valid = bounds;
        }
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);     // 1:1 pixels
        g.drawImageTransformed (image, juce::AffineTransform::scale ((float) bounds.getWidth() / (float) image.getWidth(),
                                                                     (float) bounds.getHeight() / (float) image.getHeight()), false);
    }
    bool invalidateAll() override { valid.clear(); return true; }
    bool invalidate (const juce::Rectangle<int>& area) override { valid.subtract (area); return true; }
    void releaseResources() override { image = {}; valid.clear(); }
private:
    juce::Component& owner;
    juce::Image image;
    juce::RectangleList<int> valid;
};
