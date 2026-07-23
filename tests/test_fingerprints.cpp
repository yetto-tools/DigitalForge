#include <catch2/catch_test_macros.hpp>

#include <string>

#include "components/BasicComponentLibrary.hpp"
#include "components/ComponentFingerprints.hpp"
#include "components/ComponentRegistry.hpp"
#include "core/Sha256.hpp"

using digitalforge::components::CompatibilityVerdict;
using digitalforge::components::ComponentDefinition;
using digitalforge::components::ComponentFingerprints;
using digitalforge::components::ComponentRegistry;
using digitalforge::components::compareFingerprints;
using digitalforge::components::computeFingerprints;
using digitalforge::components::PropertyValue;
using digitalforge::components::registerBasicComponentLibrary;
using digitalforge::core::Hash256;
using digitalforge::core::sha256;

namespace {

ComponentRegistry makeRegistry() {
    ComponentRegistry registry;
    registerBasicComponentLibrary(registry);
    return registry;
}

} // namespace

TEST_CASE("SHA-256 matches the published FIPS 180-4 vectors", "[core][sha256]") {
    // Sin estos vectores, una implementacion propia puede ser perfectamente
    // determinista y aun asi estar mal - y las huellas dejarian de coincidir
    // con las de cualquier otra herramienta.
    CHECK(sha256("").toHex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256("abc").toHex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").toHex() ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // Mensaje de mas de un bloque (64 bytes), que ejercita el relleno con dos
    // bloques de cola.
    CHECK(sha256(std::string(1000, 'a')).toHex() ==
          "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST_CASE("Hash256 round-trips through hex and rejects malformed text", "[core][sha256]") {
    const Hash256 original = sha256("digitalforge");
    Hash256 parsed;
    REQUIRE(Hash256::fromHex(original.toHex(), parsed));
    CHECK(parsed == original);

    Hash256 untouched = original;
    CHECK_FALSE(Hash256::fromHex("no-es-un-hash", untouched));
    CHECK_FALSE(Hash256::fromHex(std::string(63, 'a'), untouched));
    CHECK_FALSE(Hash256::fromHex(std::string(64, 'z'), untouched));
    CHECK(untouched == original); // un hash corrupto no debe pisar el anterior

    CHECK(Hash256{}.isNull());
    CHECK_FALSE(original.isNull());
}

TEST_CASE("Fingerprints are stable and independent of presentation text", "[components][fingerprints]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentDefinition& andGate = registry.definition("gates.and");

    const ComponentFingerprints first = computeFingerprints(andGate, registry.create("gates.and", 0).properties());
    const ComponentFingerprints second = computeFingerprints(andGate, registry.create("gates.and", 1).properties());
    CHECK(first == second);
    CHECK(compareFingerprints(first, second) == CompatibilityVerdict::Identical);

    // Cambiar textos de ayuda no puede marcar un proyecto como incompatible.
    ComponentDefinition retouched = andGate;
    retouched.displayName = "Puerta Y";
    retouched.description = "Otra redaccion";
    retouched.properties[0].displayName = "Cantidad de entradas (2-64)";
    const ComponentFingerprints retouchedPrints =
        computeFingerprints(retouched, registry.create("gates.and", 2).properties());
    CHECK(compareFingerprints(first, retouchedPrints) == CompatibilityVerdict::Identical);
}

TEST_CASE("Pin fingerprint follows the derived pins, not the stored order", "[components][fingerprints]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentDefinition& andGate = registry.definition("gates.and");

    // Los pines se derivan de las propiedades: dos anchos distintos son dos
    // contratos de pines distintos.
    const auto twoInputs = registry.create("gates.and", 0, {{"inputCount", PropertyValue{uint64_t{2}}}});
    const auto threeInputs = registry.create("gates.and", 1, {{"inputCount", PropertyValue{uint64_t{3}}}});
    const ComponentFingerprints twoPrints = computeFingerprints(andGate, twoInputs.properties());
    const ComponentFingerprints threePrints = computeFingerprints(andGate, threeInputs.properties());
    CHECK(twoPrints.pinInterface != threePrints.pinInterface);
    CHECK(compareFingerprints(twoPrints, threePrints) == CompatibilityVerdict::RequiresMigration);

    // Reordenar los pines sin tocar sus claves ni direcciones NO es un cambio
    // de interfaz: reconectar por indice es justamente lo que hay que evitar.
    ComponentDefinition reordered = andGate;
    reordered.derivePins = [original = andGate.derivePins](const digitalforge::components::PropertyMap& properties) {
        auto pins = original(properties);
        std::reverse(pins.begin(), pins.end());
        return pins;
    };
    const ComponentFingerprints reorderedPrints = computeFingerprints(reordered, twoInputs.properties());
    CHECK(reorderedPrints.pinInterface == twoPrints.pinInterface);
}

TEST_CASE("Each kind of change is classified by its own fingerprint", "[components][fingerprints]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentDefinition& original = registry.definition("io.led");
    const auto properties = registry.create("io.led", 0).properties();
    const ComponentFingerprints base = computeFingerprints(original, properties);

    // Solo cambio el dibujo: no debe tocar conexiones ni obligar a
    // reconstruir la simulacion.
    ComponentDefinition redrawn = original;
    redrawn.appearanceVersion = original.appearanceVersion + 1;
    const ComponentFingerprints redrawnPrints = computeFingerprints(redrawn, properties);
    CHECK(redrawnPrints.appearance != base.appearance);
    CHECK(redrawnPrints.simulation == base.simulation);
    CHECK(redrawnPrints.publicInterface == base.publicInterface);
    CHECK(compareFingerprints(base, redrawnPrints) == CompatibilityVerdict::AppearanceOnly);

    // Solo cambio el comportamiento interno: hay que recompilar, pero las
    // conexiones y las propiedades siguen siendo validas.
    ComponentDefinition rewired = original;
    rewired.behaviorVersion = original.behaviorVersion + 1;
    const ComponentFingerprints rewiredPrints = computeFingerprints(rewired, properties);
    CHECK(rewiredPrints.simulation != base.simulation);
    CHECK(rewiredPrints.publicInterface == base.publicInterface);
    CHECK(compareFingerprints(base, rewiredPrints) == CompatibilityVerdict::RequiresRecompile);

    // Cambio la interfaz publica: migracion, y manda sobre cualquier otro
    // cambio que venga junto.
    ComponentDefinition retyped = original;
    retyped.definitionVersion = original.definitionVersion + 1;
    retyped.appearanceVersion = original.appearanceVersion + 1;
    retyped.behaviorVersion = original.behaviorVersion + 1;
    const ComponentFingerprints retypedPrints = computeFingerprints(retyped, properties);
    CHECK(compareFingerprints(base, retypedPrints) == CompatibilityVerdict::RequiresMigration);
}

TEST_CASE("Adding an optional property is detected as a public interface change",
          "[components][fingerprints]") {
    // Es exactamente el cambio que se le hizo a io.ledMatrix al agregarle la
    // propiedad "wiring": compatible en la practica porque tiene valor por
    // defecto, pero la interfaz publica cambio y el proyecto tiene que poder
    // enterarse en vez de asumirlo en silencio.
    ComponentRegistry registry = makeRegistry();
    const ComponentDefinition& matrix = registry.definition("io.ledMatrix");
    const auto properties = registry.create("io.ledMatrix", 0).properties();
    const ComponentFingerprints current = computeFingerprints(matrix, properties);

    ComponentDefinition previous = matrix;
    const auto removed = std::remove_if(previous.properties.begin(), previous.properties.end(),
                                         [](const digitalforge::components::PropertyDescriptor& descriptor) {
                                             return descriptor.id == "wiring";
                                         });
    previous.properties.erase(removed, previous.properties.end());

    const ComponentFingerprints before = computeFingerprints(previous, properties);
    CHECK(before.propertyInterface != current.propertyInterface);
    CHECK(compareFingerprints(before, current) == CompatibilityVerdict::RequiresMigration);
}

TEST_CASE("Physical package is part of the public contract of a 74LS chip",
          "[components][fingerprints]") {
    ComponentRegistry registry = makeRegistry();
    const ComponentDefinition& chip = registry.definition("ic74ls.hexInverter");
    const auto properties = registry.create("ic74ls.hexInverter", 0).properties();
    const ComponentFingerprints base = computeFingerprints(chip, properties);
    CHECK_FALSE(base.package.isNull());

    // Cambiar la numeracion fisica del DIP cambia el chip, aunque los pines
    // logicos sean los mismos.
    ComponentDefinition repackaged = chip;
    REQUIRE(repackaged.physicalPinout.size() >= 2);
    std::swap(repackaged.physicalPinout[0], repackaged.physicalPinout[1]);
    const ComponentFingerprints repackagedPrints = computeFingerprints(repackaged, properties);
    CHECK(repackagedPrints.package != base.package);
    CHECK(compareFingerprints(base, repackagedPrints) == CompatibilityVerdict::RequiresMigration);
}
