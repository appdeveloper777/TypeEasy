# Publicar release v0.0.1 (Windows + Linux)

Esta guia deja TypeEasy listo para distribuirse sin Docker en Windows y Ubuntu.

## Windows

## 1) Compilar binario nativo

En MSYS2 MINGW64:

```bash
bash scripts/build_native_windows.sh
```

Resultado esperado: `src/typeeasy.exe`.

## 2) Crear carpeta de release

```bash
bash scripts/package_windows_release.sh 0.0.1
```

Resultado esperado: `dist/windows/TypeEasy-0.0.1-win64`.

## 3) Generar instalador

En PowerShell (con Inno Setup instalado):

```powershell
.\scripts\build_windows_installer.ps1 -Version 0.0.1
```

Resultado esperado: `dist/windows/TypeEasy-Setup-v0.0.1.exe`.

## 4) Generar ZIP portable + hashes

```powershell
.\scripts\write_release_hashes.ps1 -Version 0.0.1
```

Resultados esperados:

- `dist/windows/TypeEasy-0.0.1-win64.zip`
- `dist/windows/SHA256SUMS-0.0.1.txt`

## Linux (Ubuntu/Debian)

### 1) Compilar binario nativo

```bash
sudo apt-get install -y build-essential flex bison pkg-config \
                        libmariadb-dev libpq-dev freetds-dev
bash scripts/build_native_linux.sh
```

Resultado esperado: `src/typeeasy`.

### 2) Generar tar.gz + .deb + hashes

```bash
sudo apt-get install -y dpkg-dev
bash scripts/package_linux_release.sh 0.0.1
```

Resultados esperados:

- `dist/linux/TypeEasy-0.0.1-linux-amd64.tar.gz`
- `dist/linux/typeeasy_0.0.1_amd64.deb`
- `dist/linux/SHA256SUMS-0.0.1.txt`

### 3) Instalar el .deb en Ubuntu

```bash
sudo apt-get install ./dist/linux/typeeasy_0.0.1_amd64.deb
typeeasy /usr/share/typeeasy/examples/crear_const_variable.te
```

## Publicar en GitHub Releases (ambas plataformas)

Crear tag:

```bash
git tag v0.0.1
git push origin v0.0.1
```

Los workflows publican automaticamente:

- `Release - Windows Installer`: `.exe`, ZIP portable y SHA256SUMS.
- `Release - Linux Installer`: `.tar.gz`, `.deb` y SHA256SUMS.
