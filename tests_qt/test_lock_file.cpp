// Verifica el archivo de bloqueo digitalforge.lock.json: que registre las
// versiones exactas de los componentes usados, que se escriba junto al
// proyecto al guardar, y que comparar dos locks detecte cambios silenciosos.

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QTemporaryDir>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

#include "editor/CircuitDocument.hpp"
#include "editor/Project.hpp"
#include "formats/FormatVersions.hpp"
#include "formats/LockFile.hpp"

using digitalforge::editor::CircuitDocument;
using digitalforge::editor::ComponentPlacement;
using digitalforge::editor::Project;
namespace formats = digitalforge::formats;

namespace {

// El typeId aparece en el lock (varias instancias del mismo tipo -> una
// entrada).
const formats::LockedComponent* findType(const formats::LockFile& lock, const std::string& typeId) {
    for (const formats::LockedComponent& c : lock.components) {
        if (c.typeId == typeId) {
            return &c;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("A lock file lists each used component type once with its exact version",
          "[formats][lockfile]") {
    Project project;
    CircuitDocument* document = project.activeDocument();
    REQUIRE(document != nullptr);
    document->addComponent("gates.and", {}, ComponentPlacement{QPointF(0, 0), 0});
    document->addComponent("gates.and", {}, ComponentPlacement{QPointF(40, 0), 0}); // repetido a proposito
    document->addComponent("io.led", {}, ComponentPlacement{QPointF(80, 0), 0});

    const formats::LockFile lock = formats::buildLockFile(project, "0.1.0");

    CHECK(lock.lockFormatVersion == formats::versions::kLockSchema);
    CHECK(lock.digitalForgeVersion == "0.1.0");
    // Dos instancias de gates.and pero una sola entrada.
    CHECK(lock.components.size() == 2);

    const formats::LockedComponent* andEntry = findType(lock, "gates.and");
    REQUIRE(andEntry != nullptr);
    CHECK(andEntry->definitionVersion >= 1);
    CHECK(andEntry->publicInterfaceHash.size() == 64);
    CHECK(andEntry->simulationHash.size() == 64);

    // Una biblioteca (la integrada) con su huella de contenido.
    REQUIRE(lock.libraries.size() == 1);
    CHECK(lock.libraries[0].contentHash.size() == 64);
}

TEST_CASE("An empty project produces a lock with no components or libraries", "[formats][lockfile]") {
    Project project;
    const formats::LockFile lock = formats::buildLockFile(project, "0.1.0");
    CHECK(lock.components.empty());
    CHECK(lock.libraries.empty());
}

TEST_CASE("A lock file round-trips through JSON", "[formats][lockfile]") {
    Project project;
    project.activeDocument()->addComponent("gates.or", {}, ComponentPlacement{QPointF(0, 0), 0});

    const formats::LockFile original = formats::buildLockFile(project, "0.1.0");
    const formats::LockFile reparsed = formats::parseLockFile(formats::serializeLockFile(original));

    REQUIRE(reparsed.components.size() == original.components.size());
    CHECK(reparsed.lockFormatVersion == original.lockFormatVersion);
    CHECK(reparsed.digitalForgeVersion == original.digitalForgeVersion);
    CHECK(reparsed.components[0].typeId == original.components[0].typeId);
    CHECK(reparsed.components[0].publicInterfaceHash == original.components[0].publicInterfaceHash);
}

TEST_CASE("Saving a project writes the lock file next to it", "[formats][lockfile]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectPath = dir.filePath("demo.dfproj");

    Project project;
    project.activeDocument()->addComponent("gates.and", {}, ComponentPlacement{QPointF(0, 0), 0});
    project.saveToFile(projectPath);

    const QString lockPath = dir.filePath(formats::kLockFileName);
    REQUIRE(QFile::exists(lockPath));

    std::ifstream in(lockPath.toStdString());
    nlohmann::json json;
    in >> json;
    CHECK(json.at("lockFormatVersion").get<uint32_t>() == formats::versions::kLockSchema);
    CHECK(json.at("components").size() == 1);
}

TEST_CASE("Comparing lock files detects changed, added and removed component types",
          "[formats][lockfile]") {
    Project base;
    base.activeDocument()->addComponent("gates.and", {}, ComponentPlacement{QPointF(0, 0), 0});
    base.activeDocument()->addComponent("io.led", {}, ComponentPlacement{QPointF(40, 0), 0});
    const formats::LockFile stored = formats::buildLockFile(base, "0.1.0");

    SECTION("identical environment reproduces without differences") {
        const formats::LockFile current = formats::buildLockFile(base, "0.1.0");
        CHECK(formats::compareLockFiles(stored, current).empty());
    }

    SECTION("a silently changed fingerprint is flagged") {
        formats::LockFile current = stored;
        current.components.front().simulationHash = std::string(64, 'f');
        const auto diffs = formats::compareLockFiles(stored, current);
        REQUIRE(diffs.size() == 1);
        CHECK(diffs[0].kind == formats::LockComparisonKind::Changed);
    }

    SECTION("a newly used type is reported as added") {
        Project more;
        more.activeDocument()->addComponent("gates.and", {}, ComponentPlacement{QPointF(0, 0), 0});
        more.activeDocument()->addComponent("io.led", {}, ComponentPlacement{QPointF(40, 0), 0});
        more.activeDocument()->addComponent("gates.or", {}, ComponentPlacement{QPointF(80, 0), 0});
        const formats::LockFile current = formats::buildLockFile(more, "0.1.0");

        const auto diffs = formats::compareLockFiles(stored, current);
        REQUIRE(diffs.size() == 1);
        CHECK(diffs[0].kind == formats::LockComparisonKind::Added);
        CHECK(diffs[0].typeId == "gates.or");
    }

    SECTION("a type no longer used is reported as removed") {
        Project fewer;
        fewer.activeDocument()->addComponent("gates.and", {}, ComponentPlacement{QPointF(0, 0), 0});
        const formats::LockFile current = formats::buildLockFile(fewer, "0.1.0");

        const auto diffs = formats::compareLockFiles(stored, current);
        REQUIRE(diffs.size() == 1);
        CHECK(diffs[0].kind == formats::LockComparisonKind::Removed);
        CHECK(diffs[0].typeId == "io.led");
    }
}
