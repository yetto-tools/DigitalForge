#include "FlipFlopExcitation.hpp"

namespace digitalforge::editor {

namespace {

// D = Q+ -- pero si el estado actual de esa fila es DontCare (inalcanzable),
// tambien lo es la excitacion, aunque el estado siguiente este definido:
// no tiene sentido pedir un valor de entrada concreto para llegar a un
// estado siguiente partiendo de un estado que nunca ocurre.
std::vector<KarnaughCellValue> excitationD(const std::vector<KarnaughCellValue>& presentState,
                                            const std::vector<KarnaughCellValue>& nextState) {
    std::vector<KarnaughCellValue> result(nextState.size());
    for (std::size_t m = 0; m < nextState.size(); ++m) {
        result[m] = (presentState[m] == KarnaughCellValue::DontCare) ? KarnaughCellValue::DontCare : nextState[m];
    }
    return result;
}

// T = 0 si Q+ == Q (no cambia), 1 si Q+ != Q (cambia).
std::vector<KarnaughCellValue> excitationT(const std::vector<KarnaughCellValue>& presentState,
                                            const std::vector<KarnaughCellValue>& nextState) {
    std::vector<KarnaughCellValue> result(nextState.size());
    for (std::size_t m = 0; m < nextState.size(); ++m) {
        const KarnaughCellValue present = presentState[m];
        const KarnaughCellValue next = nextState[m];
        if (present == KarnaughCellValue::DontCare || next == KarnaughCellValue::DontCare) {
            result[m] = KarnaughCellValue::DontCare;
            continue;
        }
        result[m] = (next == present) ? KarnaughCellValue::Zero : KarnaughCellValue::One;
    }
    return result;
}

// Tabla de excitacion JK estandar:
//   Q=0,Q+=0 -> J=0,K=X   Q=0,Q+=1 -> J=1,K=X
//   Q=1,Q+=0 -> J=X,K=1   Q=1,Q+=1 -> J=X,K=0
void excitationJK(const std::vector<KarnaughCellValue>& presentState, const std::vector<KarnaughCellValue>& nextState,
                   std::vector<KarnaughCellValue>& j, std::vector<KarnaughCellValue>& k) {
    j.resize(nextState.size());
    k.resize(nextState.size());
    for (std::size_t m = 0; m < nextState.size(); ++m) {
        const KarnaughCellValue present = presentState[m];
        const KarnaughCellValue next = nextState[m];
        if (present == KarnaughCellValue::DontCare || next == KarnaughCellValue::DontCare) {
            j[m] = KarnaughCellValue::DontCare;
            k[m] = KarnaughCellValue::DontCare;
            continue;
        }
        if (present == KarnaughCellValue::Zero) {
            j[m] = next; // 0->0: J=0 ; 0->1: J=1
            k[m] = KarnaughCellValue::DontCare;
        } else {
            j[m] = KarnaughCellValue::DontCare;
            k[m] = (next == KarnaughCellValue::Zero) ? KarnaughCellValue::One : KarnaughCellValue::Zero;
        }
    }
}

// Tabla de excitacion SR estandar (S=R=1 nunca se pide):
//   Q=0,Q+=0 -> S=0,R=X   Q=0,Q+=1 -> S=1,R=0
//   Q=1,Q+=0 -> S=0,R=1   Q=1,Q+=1 -> S=X,R=0
void excitationSR(const std::vector<KarnaughCellValue>& presentState, const std::vector<KarnaughCellValue>& nextState,
                   std::vector<KarnaughCellValue>& s, std::vector<KarnaughCellValue>& r) {
    s.resize(nextState.size());
    r.resize(nextState.size());
    for (std::size_t m = 0; m < nextState.size(); ++m) {
        const KarnaughCellValue present = presentState[m];
        const KarnaughCellValue next = nextState[m];
        if (present == KarnaughCellValue::DontCare || next == KarnaughCellValue::DontCare) {
            s[m] = KarnaughCellValue::DontCare;
            r[m] = KarnaughCellValue::DontCare;
            continue;
        }
        if (present == KarnaughCellValue::Zero) {
            s[m] = next; // 0->0: S=0 ; 0->1: S=1
            r[m] = (next == KarnaughCellValue::One) ? KarnaughCellValue::Zero : KarnaughCellValue::DontCare;
        } else {
            s[m] = (next == KarnaughCellValue::Zero) ? KarnaughCellValue::Zero : KarnaughCellValue::DontCare;
            r[m] = (next == KarnaughCellValue::Zero) ? KarnaughCellValue::One : KarnaughCellValue::Zero;
        }
    }
}

} // namespace

std::vector<ExcitationColumn> computeExcitation(FlipFlopType type, const QString& bitName,
                                                 const std::vector<KarnaughCellValue>& presentState,
                                                 const std::vector<KarnaughCellValue>& nextState) {
    switch (type) {
        case FlipFlopType::D:
            return {ExcitationColumn{QStringLiteral("D") + bitName, excitationD(presentState, nextState)}};
        case FlipFlopType::T:
            return {ExcitationColumn{QStringLiteral("T") + bitName, excitationT(presentState, nextState)}};
        case FlipFlopType::JK: {
            std::vector<KarnaughCellValue> j;
            std::vector<KarnaughCellValue> k;
            excitationJK(presentState, nextState, j, k);
            return {ExcitationColumn{QStringLiteral("J") + bitName, std::move(j)},
                    ExcitationColumn{QStringLiteral("K") + bitName, std::move(k)}};
        }
        case FlipFlopType::SR: {
            std::vector<KarnaughCellValue> s;
            std::vector<KarnaughCellValue> r;
            excitationSR(presentState, nextState, s, r);
            return {ExcitationColumn{QStringLiteral("S") + bitName, std::move(s)},
                    ExcitationColumn{QStringLiteral("R") + bitName, std::move(r)}};
        }
    }
    return {};
}

} // namespace digitalforge::editor
