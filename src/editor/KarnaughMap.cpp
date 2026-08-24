#include "KarnaughMap.hpp"

#include <QStringList>

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace digitalforge::editor {

namespace {

int popcount(uint32_t value) {
    int count = 0;
    while (value != 0) {
        count += static_cast<int>(value & 1U);
        value >>= 1U;
    }
    return count;
}

// Codificacion Gray estandar (n-esimo valor <-> n-esima posicion de la
// grilla): ver mintermAt()/gridPositionOf(), que la usan para que celdas
// fisicamente adyacentes en la grilla (incluida la voltereta de un borde al
// opuesto) difieran siempre en un solo bit.
uint32_t binaryToGray(uint32_t n) { return n ^ (n >> 1U); }

uint32_t grayToBinary(uint32_t g) {
    uint32_t b = g;
    while (g != 0) {
        g >>= 1U;
        b ^= g;
    }
    return b;
}

void validateVariableCount(int variableCount) {
    if (variableCount < 2 || variableCount > 4) {
        throw std::invalid_argument("KarnaughMap: variableCount debe estar entre 2 y 4");
    }
}

QString formatTerm(const std::vector<KarnaughLiteral>& literals, const std::vector<QString>& variableNames) {
    QStringList parts;
    for (const KarnaughLiteral& literal : literals) {
        parts << (literal.negated ? variableNames[static_cast<std::size_t>(literal.variableIndex)] + QStringLiteral("'")
                                   : variableNames[static_cast<std::size_t>(literal.variableIndex)]);
    }
    return parts.join(QStringLiteral("·"));
}

QString formatMintermList(const std::vector<int>& minterms) {
    QStringList parts;
    for (int m : minterms) {
        parts << QString::number(m);
    }
    return parts.join(QStringLiteral(","));
}

// Un implicante "en construccion" durante Quine-McCluskey: ademas de los
// campos persistidos en Implicant, necesita saber si ya se combino con
// otro (si no, es primo) - ver el bucle principal de minimize().
struct WorkingImplicant {
    Implicant implicant;
    bool combined = false;
};

// Agrupa `implicants` por la cantidad de bits en 1 en `.bits` (0 en las
// posiciones ya eliminadas, asi que popcount(bits) es estable sin importar
// dashMask) - el bucketing que exige el algoritmo de QM para comparar solo
// implicantes de tamanos de grupo adyacentes en cada ronda.
std::map<int, std::vector<std::size_t>> bucketByPopcount(const std::vector<WorkingImplicant>& implicants) {
    std::map<int, std::vector<std::size_t>> buckets;
    for (std::size_t i = 0; i < implicants.size(); ++i) {
        buckets[popcount(implicants[i].implicant.bits)].push_back(i);
    }
    return buckets;
}

bool sameImplicant(const Implicant& a, const Implicant& b) { return a.bits == b.bits && a.dashMask == b.dashMask; }

} // namespace

std::vector<KarnaughLiteral> Implicant::literals(int variableCount) const {
    std::vector<KarnaughLiteral> result;
    for (int i = 0; i < variableCount; ++i) {
        if (((dashMask >> i) & 1U) != 0) {
            continue;
        }
        result.push_back(KarnaughLiteral{i, ((bits >> i) & 1U) == 0});
    }
    return result;
}

std::pair<int, int> gridDimensions(int variableCount) {
    validateVariableCount(variableCount);
    const int rowVarCount = variableCount / 2;
    const int colVarCount = variableCount - rowVarCount;
    return {1 << rowVarCount, 1 << colVarCount};
}

int mintermAt(int row, int col, int variableCount) {
    const auto [rows, cols] = gridDimensions(variableCount);
    if (row < 0 || row >= rows || col < 0 || col >= cols) {
        throw std::invalid_argument("KarnaughMap::mintermAt: posicion fuera de la grilla");
    }
    const int rowVarCount = variableCount / 2;
    const uint32_t rowBits = binaryToGray(static_cast<uint32_t>(row));
    const uint32_t colBits = binaryToGray(static_cast<uint32_t>(col));
    return static_cast<int>(rowBits | (colBits << rowVarCount));
}

std::pair<int, int> gridPositionOf(int minterm, int variableCount) {
    validateVariableCount(variableCount);
    const int mintermCount = 1 << variableCount;
    if (minterm < 0 || minterm >= mintermCount) {
        throw std::invalid_argument("KarnaughMap::gridPositionOf: minterm fuera de rango");
    }
    const int rowVarCount = variableCount / 2;
    const uint32_t rowMask = (1U << rowVarCount) - 1U;
    const uint32_t rowBits = static_cast<uint32_t>(minterm) & rowMask;
    const uint32_t colBits = static_cast<uint32_t>(minterm) >> rowVarCount;
    return {static_cast<int>(grayToBinary(rowBits)), static_cast<int>(grayToBinary(colBits))};
}

KarnaughResult minimize(int variableCount, const std::vector<QString>& variableNames,
                         const std::vector<KarnaughCellValue>& cellsByMinterm) {
    validateVariableCount(variableCount);
    if (static_cast<int>(variableNames.size()) != variableCount) {
        throw std::invalid_argument("KarnaughMap::minimize: variableNames.size() debe ser igual a variableCount");
    }
    const std::size_t mintermCount = std::size_t{1} << variableCount;
    if (cellsByMinterm.size() != mintermCount) {
        throw std::invalid_argument("KarnaughMap::minimize: cellsByMinterm.size() debe ser 2^variableCount");
    }

    KarnaughResult result;
    result.variableCount = variableCount;
    result.variableNames = variableNames;

    // --- Quine-McCluskey: generar implicantes primos ------------------------
    std::vector<WorkingImplicant> current;
    for (std::size_t m = 0; m < mintermCount; ++m) {
        if (cellsByMinterm[m] == KarnaughCellValue::Zero) {
            continue;
        }
        Implicant implicant;
        implicant.bits = static_cast<uint32_t>(m);
        implicant.dashMask = 0;
        implicant.minterms = {static_cast<int>(m)};
        current.push_back(WorkingImplicant{implicant, false});
    }

    std::vector<Implicant> primes;
    std::set<std::pair<uint32_t, uint32_t>> emittedCombinations; // (bits, dashMask) ya animado como CombinePair

    while (!current.empty()) {
        const std::map<int, std::vector<std::size_t>> buckets = bucketByPopcount(current);
        std::vector<WorkingImplicant> next;
        std::set<std::pair<uint32_t, uint32_t>> seenThisRound;

        for (auto it = buckets.begin(); it != buckets.end(); ++it) {
            const auto nextBucketIt = buckets.find(it->first + 1);
            if (nextBucketIt == buckets.end()) {
                continue;
            }
            for (std::size_t ai : it->second) {
                for (std::size_t bi : nextBucketIt->second) {
                    WorkingImplicant& a = current[ai];
                    WorkingImplicant& b = current[bi];
                    if (a.implicant.dashMask != b.implicant.dashMask) {
                        continue;
                    }
                    const uint32_t diff = a.implicant.bits ^ b.implicant.bits;
                    if (popcount(diff) != 1) {
                        continue;
                    }
                    a.combined = true;
                    b.combined = true;

                    Implicant combined;
                    combined.dashMask = a.implicant.dashMask | diff;
                    combined.bits = a.implicant.bits & ~diff;
                    std::vector<int> merged = a.implicant.minterms;
                    merged.insert(merged.end(), b.implicant.minterms.begin(), b.implicant.minterms.end());
                    std::sort(merged.begin(), merged.end());
                    merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
                    combined.minterms = merged;

                    const auto key = std::make_pair(combined.bits, combined.dashMask);
                    if (seenThisRound.insert(key).second) {
                        next.push_back(WorkingImplicant{combined, false});
                    }
                    if (emittedCombinations.insert(key).second) {
                        KarnaughStep step;
                        step.kind = KarnaughStepKind::CombinePair;
                        step.description = QStringLiteral("Combinando m(%1) con m(%2) -> %3")
                                                .arg(formatMintermList(a.implicant.minterms))
                                                .arg(formatMintermList(b.implicant.minterms))
                                                .arg(formatTerm(combined.literals(variableCount), variableNames));
                        step.involvedMinterms = combined.minterms;
                        step.implicant = combined;
                        result.steps.push_back(std::move(step));
                    }
                }
            }
        }

        for (const WorkingImplicant& w : current) {
            if (!w.combined) {
                const bool alreadyPrime =
                    std::any_of(primes.begin(), primes.end(), [&](const Implicant& p) { return sameImplicant(p, w.implicant); });
                if (!alreadyPrime) {
                    primes.push_back(w.implicant);
                    KarnaughStep step;
                    step.kind = KarnaughStepKind::MarkPrime;
                    step.description = QStringLiteral("m(%1) no se combina mas: implicante primo %2")
                                            .arg(formatMintermList(w.implicant.minterms))
                                            .arg(formatTerm(w.implicant.literals(variableCount), variableNames));
                    step.involvedMinterms = w.implicant.minterms;
                    step.implicant = w.implicant;
                    result.steps.push_back(std::move(step));
                }
            }
        }

        current = std::move(next);
    }

    result.primeImplicants = primes;

    // --- Tabla de cobertura: solo minterms en '1' (los DontCare ayudaron a
    // agrupar pero nunca justifican un termino por si solos) -----------------
    std::vector<int> requiredMinterms;
    for (std::size_t m = 0; m < mintermCount; ++m) {
        if (cellsByMinterm[m] == KarnaughCellValue::One) {
            requiredMinterms.push_back(static_cast<int>(m));
        }
    }

    std::map<int, std::vector<std::size_t>> coverage; // minterm -> indices en `primes`
    for (int m : requiredMinterms) {
        for (std::size_t p = 0; p < primes.size(); ++p) {
            if (std::find(primes[p].minterms.begin(), primes[p].minterms.end(), m) != primes[p].minterms.end()) {
                coverage[m].push_back(p);
            }
        }
    }

    std::set<std::size_t> essentialIndices;
    for (int m : requiredMinterms) {
        const auto& covering = coverage[m];
        if (covering.size() == 1) {
            essentialIndices.insert(covering.front());
        }
    }

    std::vector<Implicant> selected;
    std::set<int> coveredMinterms;
    for (std::size_t idx : essentialIndices) {
        selected.push_back(primes[idx]);
        for (int m : primes[idx].minterms) {
            coveredMinterms.insert(m);
        }
        KarnaughStep step;
        step.kind = KarnaughStepKind::SelectEssential;
        step.description = QStringLiteral("%1 es esencial: es el unico que cubre a m(%2)")
                                .arg(formatTerm(primes[idx].literals(variableCount), variableNames))
                                .arg(formatMintermList(primes[idx].minterms));
        step.involvedMinterms = primes[idx].minterms;
        step.implicant = primes[idx];
        step.groupIndex = static_cast<int>(selected.size()) - 1;
        result.steps.push_back(std::move(step));
    }

    std::vector<int> remaining;
    for (int m : requiredMinterms) {
        if (!coveredMinterms.contains(m)) {
            remaining.push_back(m);
        }
    }

    if (!remaining.empty()) {
        // Metodo de Petrick sobre los primos NO esenciales que cubren algun
        // minterm restante: producto de sumas (una suma por minterm sin
        // cubrir, sobre los primos que lo cubren) expandido a suma de
        // productos, quedandonos con el termino de menos primos (y, en
        // empate, de menos literales en total).
        std::vector<std::size_t> candidates;
        for (std::size_t p = 0; p < primes.size(); ++p) {
            if (essentialIndices.contains(p)) {
                continue;
            }
            const bool coversSomeRemaining =
                std::any_of(remaining.begin(), remaining.end(), [&](int m) {
                    return std::find(primes[p].minterms.begin(), primes[p].minterms.end(), m) != primes[p].minterms.end();
                });
            if (coversSomeRemaining) {
                candidates.push_back(p);
            }
        }

        std::vector<std::set<std::size_t>> products{{}};
        for (int m : remaining) {
            std::vector<std::size_t> coveringCandidates;
            for (std::size_t p : candidates) {
                if (std::find(primes[p].minterms.begin(), primes[p].minterms.end(), m) != primes[p].minterms.end()) {
                    coveringCandidates.push_back(p);
                }
            }
            std::vector<std::set<std::size_t>> next;
            for (const std::set<std::size_t>& existing : products) {
                for (std::size_t p : coveringCandidates) {
                    std::set<std::size_t> combined = existing;
                    combined.insert(p);
                    next.push_back(std::move(combined));
                }
            }
            // Absorcion: descarta cualquier termino que sea superconjunto de
            // otro ya presente (nunca puede ser el minimo) - mantiene la
            // expansion manejable ronda a ronda.
            std::sort(next.begin(), next.end(),
                      [](const std::set<std::size_t>& a, const std::set<std::size_t>& b) { return a.size() < b.size(); });
            std::vector<std::set<std::size_t>> pruned;
            for (const std::set<std::size_t>& candidate : next) {
                const bool dominated = std::any_of(pruned.begin(), pruned.end(), [&](const std::set<std::size_t>& kept) {
                    return std::includes(candidate.begin(), candidate.end(), kept.begin(), kept.end());
                });
                if (!dominated) {
                    pruned.push_back(candidate);
                }
            }
            products = std::move(pruned);
        }

        const std::set<std::size_t>* best = nullptr;
        int bestLiteralCount = 0;
        for (const std::set<std::size_t>& product : products) {
            int literalCount = 0;
            for (std::size_t p : product) {
                literalCount += static_cast<int>(primes[p].literals(variableCount).size());
            }
            if (best == nullptr || product.size() < best->size() ||
                (product.size() == best->size() && literalCount < bestLiteralCount)) {
                best = &product;
                bestLiteralCount = literalCount;
            }
        }

        if (best != nullptr) {
            for (std::size_t p : *best) {
                selected.push_back(primes[p]);
                KarnaughStep step;
                step.kind = KarnaughStepKind::SelectViaPetrick;
                step.description = QStringLiteral("Metodo de Petrick: agrega %1 para cubrir m(%2)")
                                        .arg(formatTerm(primes[p].literals(variableCount), variableNames))
                                        .arg(formatMintermList(primes[p].minterms));
                step.involvedMinterms = primes[p].minterms;
                step.implicant = primes[p];
                step.groupIndex = static_cast<int>(selected.size()) - 1;
                result.steps.push_back(std::move(step));
            }
        }
    }

    result.essentialPrimeImplicants.reserve(essentialIndices.size());
    for (std::size_t idx : essentialIndices) {
        result.essentialPrimeImplicants.push_back(primes[idx]);
    }
    result.selectedGroups = selected;

    for (std::size_t i = 0; i < selected.size(); ++i) {
        KarnaughStep step;
        step.kind = KarnaughStepKind::FinalTerm;
        step.description = QStringLiteral("Termino final: %1")
                                .arg(formatTerm(selected[i].literals(variableCount), variableNames));
        step.involvedMinterms = selected[i].minterms;
        step.implicant = selected[i];
        step.groupIndex = static_cast<int>(i);
        result.steps.push_back(std::move(step));
    }

    if (selected.empty()) {
        result.sopExpression = QStringLiteral("0");
    } else {
        const bool anyAlwaysTrue = std::any_of(selected.begin(), selected.end(), [&](const Implicant& implicant) {
            return implicant.literals(variableCount).empty();
        });
        if (anyAlwaysTrue) {
            result.sopExpression = QStringLiteral("1");
        } else {
            QStringList terms;
            for (const Implicant& implicant : selected) {
                terms << formatTerm(implicant.literals(variableCount), variableNames);
            }
            result.sopExpression = terms.join(QStringLiteral(" + "));
        }
    }

    return result;
}

} // namespace digitalforge::editor
