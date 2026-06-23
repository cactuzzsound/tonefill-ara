#pragma once

#include "core/Ids.h"
#include "engine/synthesis/AmbienceRenderer.h"

#include <list>
#include <memory>
#include <mutex>
#include <utility>

namespace tonefill::engine::render
{
using RenderResult    = synthesis::AmbienceRenderer::Output;
using RenderResultPtr = std::shared_ptr<const RenderResult>;

// Small LRU of rendered fills keyed by (ModelId, RenderId). Renders are cheap to
// regenerate, so this stays small. Guarded; accessed from message + render threads.
class RenderCache
{
public:
    explicit RenderCache (std::size_t maxEntries = 4) : maxEntries_ (maxEntries) {}

    RenderResultPtr get (core::ModelId model, core::RenderId render) const
    {
        std::lock_guard<std::mutex> lock (mutex_);
        for (const auto& e : entries_)
            if (e.model == model && e.render == render)
                return e.value;
        return nullptr;
    }

    void put (core::ModelId model, core::RenderId render, RenderResultPtr value)
    {
        std::lock_guard<std::mutex> lock (mutex_);
        entries_.push_front ({ model, render, std::move (value) });
        while (entries_.size() > maxEntries_)
            entries_.pop_back();
    }

    void invalidate()
    {
        std::lock_guard<std::mutex> lock (mutex_);
        entries_.clear();
    }

private:
    struct Entry { core::ModelId model; core::RenderId render; RenderResultPtr value; };

    mutable std::mutex mutex_;
    std::list<Entry>   entries_;
    std::size_t        maxEntries_;
};
} // namespace tonefill::engine::render
