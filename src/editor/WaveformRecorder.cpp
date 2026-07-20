#include "WaveformRecorder.hpp"

#include <algorithm>

namespace digitalforge::editor {

WaveformRecorder::WaveformRecorder(CircuitDocument* document, QObject* parent) : QObject(parent), document_(nullptr) {
    setDocument(document);
}

void WaveformRecorder::setDocument(CircuitDocument* document) {
    disconnect(steppedConnection_);
    disconnect(rebuiltConnection_);
    document_ = document;
    clearWatches();
    steppedConnection_ = connect(document_, &CircuitDocument::simulationStepped, this, &WaveformRecorder::sample);
    rebuiltConnection_ = connect(document_, &CircuitDocument::simulationRebuilt, this, &WaveformRecorder::reset);
}

bool WaveformRecorder::addWatch(WireEndpoint endpoint, QString label) {
    if (watches_.size() >= kMaxWatchedNets) {
        return false;
    }
    const bool alreadyWatched =
        std::any_of(watches_.begin(), watches_.end(), [&](const WatchedNet& watch) { return watch.endpoint == endpoint; });
    if (alreadyWatched) {
        return false;
    }

    WatchedNet watch;
    watch.endpoint = endpoint;
    watch.label = std::move(label);
    watch.samples.push_back(WaveformSample{nextSampleIndex_, document_->endpointValue(endpoint)});
    watches_.push_back(std::move(watch));
    emit samplesChanged();
    return true;
}

void WaveformRecorder::removeWatch(std::size_t index) {
    if (index >= watches_.size()) {
        return;
    }
    watches_.erase(watches_.begin() + static_cast<std::ptrdiff_t>(index));
    emit samplesChanged();
}

void WaveformRecorder::clearWatches() {
    watches_.clear();
    nextSampleIndex_ = 0;
    emit samplesChanged();
}

void WaveformRecorder::sample() {
    if (watches_.empty()) {
        return;
    }
    ++nextSampleIndex_;
    for (WatchedNet& watch : watches_) {
        const core::LogicValue value = document_->endpointValue(watch.endpoint);
        if (watch.samples.empty() || watch.samples.back().value != value) {
            watch.samples.push_back(WaveformSample{nextSampleIndex_, value});
        }
    }
    emit samplesChanged();
}

void WaveformRecorder::reset() {
    for (WatchedNet& watch : watches_) {
        watch.samples.clear();
    }
    nextSampleIndex_ = 0;
    emit samplesChanged();
}

} // namespace digitalforge::editor
