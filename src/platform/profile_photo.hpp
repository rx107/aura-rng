#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
namespace aura {
// 選んだ写真の中央を正方形で切り取り、通信に使う160×160 PNGへ変換します。
// 元画像やそのファイル名をサーバーへ送らず、小さい画像だけを使います。
std::vector<std::uint8_t> prepareProfilePhoto(const std::filesystem::path& source);
std::vector<std::uint8_t> readProfilePhoto(const std::filesystem::path& cached);
void saveProfilePhoto(const std::filesystem::path& cached,const std::vector<std::uint8_t>& bytes);
}
