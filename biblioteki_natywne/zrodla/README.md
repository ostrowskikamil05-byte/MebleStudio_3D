# Natywne składniki MebelStudio3D 0.14

Program wykorzystuje host C++ oraz bibliotekę widoku C++. Zachowuje dotychczasowy interfejs Tk i moduły meblarskie w tym samym procesie. Nie jest to jeszcze całkowite przepisanie programu do C++.

`host/README_HOST_PL.md` opisuje osadzone środowisko, izolację importów i budowanie głównego EXE. Biblioteka `MebelNative3D.dll` zawiera silniki DirectX 11, DirectX 12 i Vulkan oraz nakładkę wymiarów, otworów i uchwytów edycji. Oryginalny obszar Canvas utrzymuje obsługę myszy i klawiatury. Dopasowywanie, CAD, szkic, wyposażenie, produkcja i dokumentacja korzystają z zachowanej logiki aplikacji.

Interfejs `src/native_embed.h` przyjmuje gotowe trójkąty i kopiuje opisy. Dane CAD oraz produkcyjne pozostają w modelu aplikacji. Obracanie kamery korzysta z zachowanego bufora GPU; nie przebudowuje dokładnej geometrii CAD. Nakładka GDI+ odtwarza opisy i uchwyty nad natywnym oknem graficznym. Testy `tests/test_scene_native.cpp` sprawdzają format sceny, macierz kamery, wybór powierzchni, transakcyjne wczytywanie z pamięci oraz zgodność ABI x64.

## Budowanie DLL

Wymagany jest LLVM-MinGW x64. Nagłówki Vulkan oraz skompilowane shadery SPIR-V są dołączone; do zwykłego budowania DLL nie trzeba instalować Vulkan SDK. Z katalogu źródeł:

```powershell
& .\native\tools\build_embed.ps1 -Compiler 'C:\llvm-mingw\bin\x86_64-w64-mingw32-clang++.exe' -RunTests
```

Zmiany shaderów Vulkan wymagają ich ponownej kompilacji przez `src/compile_vulkan_shaders.ps1` i glslang. Licencje narzędzi oraz nagłówków znajdują się w `licenses`. Prywatny loader Vulkan w folderze głównego programu `runtime` pochodzi z oficjalnej paczki LunarG; pochodzenie i hash zapisano w `PROVENANCE.json`.

Wybór API w ustawieniach zależy od możliwości karty i sterownika. Dotychczasowy silnik OpenGL z trybem wysokiej jakości i renderer CPU pozostają dostępne. Natywny renderer ma własny wygląd materiałów i emisję LED; nie odtwarza wszystkich efektów oświetlenia wysokiej jakości silnika OpenGL.
