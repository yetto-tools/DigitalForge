# En sandboxes sin acceso a red (p.ej. el build de Flatpak), nlohmann_json
# queda preinstalado en el prefijo de instalacion antes de configurar este
# proyecto, asi que find_package lo encuentra y se evita el FetchContent.
find_package(nlohmann_json 3.11 CONFIG QUIET)

if(NOT nlohmann_json_FOUND)
    include(FetchContent)

    set(JSON_BuildTests OFF CACHE INTERNAL "")

    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()
