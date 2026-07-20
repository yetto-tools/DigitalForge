#pragma once

#include <cstdint>
#include <queue>
#include <vector>

#include "Event.hpp"

namespace digitalforge::core {

// Min-heap de eventos pendientes ordenados por timestamp; los eventos que
// comparten un timestamp se entregan en orden FIFO (de inserción) mediante
// un número de secuencia monótonamente creciente, de modo que los resultados
// de la simulación sean deterministas.
class EventQueue {
public:
    void push(const SimulationEvent& event) {
        heap_.push(Entry{event, sequence_++});
    }

    [[nodiscard]] bool empty() const noexcept { return heap_.empty(); }

    [[nodiscard]] std::size_t size() const noexcept { return heap_.size(); }

    // Elimina y devuelve el evento programado más temprano e insertado más
    // temprano. Precondición: !empty().
    [[nodiscard]] SimulationEvent pop() {
        Entry top = heap_.top();
        heap_.pop();
        return top.event;
    }

    void clear() {
        heap_ = {};
        sequence_ = 0;
    }

private:
    struct Entry {
        SimulationEvent event;
        uint64_t sequence = 0;
    };

    struct EntryOrder {
        [[nodiscard]] bool operator()(const Entry& lhs, const Entry& rhs) const noexcept {
            if (lhs.event.timestamp != rhs.event.timestamp) {
                return lhs.event.timestamp > rhs.event.timestamp;
            }
            return lhs.sequence > rhs.sequence;
        }
    };

    std::priority_queue<Entry, std::vector<Entry>, EntryOrder> heap_;
    uint64_t sequence_ = 0;
};

} // namespace digitalforge::core
