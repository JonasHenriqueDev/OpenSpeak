#pragma once
// Versão do OpenSpeak (OPENSPEAK_RELEASE_VERSION do CMake). Fica num único arquivo
// gerado (Version.cpp.in) para que trocar a versão recompile só ele e refaça o link.

#include <QString>

namespace openspeak {
QString version();
}
