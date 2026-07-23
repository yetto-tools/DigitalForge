#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace digitalforge::core {

// Digesto de 256 bits. Se usa como huella de identidad/compatibilidad (ver
// components/ComponentFingerprints.hpp) y, mas adelante, para verificar
// recursos y paquetes de biblioteca.
struct Hash256 {
    std::array<uint8_t, 32> bytes{};

    [[nodiscard]] bool operator==(const Hash256& other) const noexcept { return bytes == other.bytes; }
    [[nodiscard]] bool operator!=(const Hash256& other) const noexcept { return !(*this == other); }

    // Representacion hexadecimal en minusculas, 64 caracteres. Es la forma en
    // que las huellas se guardan en los proyectos y se muestran al usuario.
    [[nodiscard]] std::string toHex() const;

    // Inversa de toHex(). Devuelve false (y deja `out` intacto) si el texto no
    // son exactamente 64 digitos hexadecimales - un proyecto con una huella
    // corrupta debe tratarse como "sin huella", no como una huella distinta.
    [[nodiscard]] static bool fromHex(std::string_view hex, Hash256& out);

    // True si la huella nunca fue calculada (todo ceros): lo que se lee de un
    // proyecto guardado antes de que existieran las huellas.
    [[nodiscard]] bool isNull() const noexcept;
};

// SHA-256 de un bloque de bytes. Implementacion propia y autocontenida (FIPS
// 180-4): core/ y components/ se compilan sin Qt ni dependencias externas,
// asi que no puede apoyarse en QCryptographicHash, y las huellas tienen que
// dar el mismo resultado en cualquier plataforma y compilador.
[[nodiscard]] Hash256 sha256(std::string_view data);

// Acumulador para hashear una secuencia de campos sin construir antes toda la
// cadena canonica en memoria. Cada field() inyecta ademas un separador y la
// longitud del valor, de modo que dos campos distintos no puedan producir la
// misma entrada por concatenacion (p. ej. {"ab","c"} contra {"a","bc"}).
class Sha256Builder {
public:
    Sha256Builder& field(std::string_view value);
    Sha256Builder& field(uint64_t value);
    Sha256Builder& field(bool value);

    [[nodiscard]] Hash256 finish() const;

private:
    std::string canonical_;
};

} // namespace digitalforge::core
