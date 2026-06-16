#ifndef AppVersion
#define AppVersion "0.0.1"
#endif

#ifndef SourceDir
#define SourceDir "..\\..\\dist\\windows\\TypeEasy-0.0.1-win64"
#endif

#ifndef OutputDir
#define OutputDir "..\\..\\dist\\windows"
#endif

[Setup]
AppId={{E60A47DC-5C8F-4D50-A2CF-0B86E8A70517}
AppName=TypeEasy
AppVersion={#AppVersion}
AppPublisher=TypeEasy
DefaultDirName={autopf}\TypeEasy
DefaultGroupName=TypeEasy
OutputDir={#OutputDir}
OutputBaseFilename=TypeEasy-Setup-v{#AppVersion}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
ChangesEnvironment=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\\Spanish.isl"

[Types]
Name: "full";    Description: "Instalación completa (con conector SQL Server)"
Name: "compact"; Description: "Instalación mínima (sin SQL Server, más liviana)"
Name: "custom";  Description: "Personalizada"; Flags: iscustom

[Components]
; 'core' siempre se instala (intérprete, CLI, ejemplos, MySQL/SQLite).
Name: "core"; Description: "TypeEasy (intérprete + CLI)"; Types: full compact custom; Flags: fixed
; 'sqlserver' es OPCIONAL: agrega las DLLs de FreeTDS/GnuTLS (~varios MB) para
; el conector sqlserver_*(). Desmarcarlo deja el paquete más liviano. Solo tiene
; efecto si el build incluyó SQL Server (si no, no hay DLLs que instalar).
Name: "sqlserver"; Description: "Conector SQL Server (FreeTDS) — DLLs extra, varios MB"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Crear acceso directo en el escritorio"; GroupDescription: "Opciones adicionales:"; Flags: unchecked
Name: "addtopath"; Description: "Agregar TypeEasy al PATH (recomendado)"; GroupDescription: "Opciones adicionales:";

[Files]
; Empaquetado por scripts/package_windows_release.sh:
;   {app}\bin\typeeasy-bin.exe  (interprete C)
;   {app}\bin\typeeasy.cmd      (smart dispatcher Rails-style)
;   {app}\bin\te.cmd            (alias)
;   {app}\cli\typeeasy          (bash script invocado por el .cmd via Git Bash)
;   {app}\cli\templates\        (scaffolds para 'typeeasy new')
;   {app}\vscode\typeeasy-debug.vsix  (extension VS Code; 'te ext install' la instala)
Source: "{#SourceDir}\\*"; DestDir: "{app}"; Components: core; Flags: recursesubdirs createallsubdirs ignoreversion; Excludes: "bin\\libsybdb*.dll,bin\\libgnutls*.dll,bin\\libhogweed*.dll,bin\\libnettle*.dll,bin\\libtasn1*.dll,bin\\libp11-kit*.dll,bin\\libidn2*.dll,bin\\libunistring*.dll,bin\\libgmp*.dll"
; DLLs exclusivas del conector SQL Server (FreeTDS + cadena GnuTLS). Se instalan
; solo si el usuario marca el componente 'sqlserver'. skipifsourcedoesntexist:
; si el build fue liviano (sin FreeTDS), estas líneas no fallan, simplemente no
; agregan nada.
Source: "{#SourceDir}\\bin\\libsybdb*.dll";     DestDir: "{app}\\bin"; Components: sqlserver; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\\bin\\libgnutls*.dll";    DestDir: "{app}\\bin"; Components: sqlserver; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\\bin\\libhogweed*.dll";   DestDir: "{app}\\bin"; Components: sqlserver; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\\bin\\libnettle*.dll";    DestDir: "{app}\\bin"; Components: sqlserver; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\\bin\\libtasn1*.dll";     DestDir: "{app}\\bin"; Components: sqlserver; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\\bin\\libp11-kit*.dll";   DestDir: "{app}\\bin"; Components: sqlserver; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\\bin\\libidn2*.dll";      DestDir: "{app}\\bin"; Components: sqlserver; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\\bin\\libunistring*.dll"; DestDir: "{app}\\bin"; Components: sqlserver; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\\bin\\libgmp*.dll";       DestDir: "{app}\\bin"; Components: sqlserver; Flags: ignoreversion skipifsourcedoesntexist

[InstallDelete]
; Limpia binarios .exe de versiones previas (<= 0.0.10 primera build) que
; quedarian con prioridad sobre los .cmd nuevos via PATHEXT.
Type: files; Name: "{app}\\bin\\typeeasy.exe"
Type: files; Name: "{app}\\bin\\te.exe"

[Icons]
Name: "{autoprograms}\\TypeEasy"; Filename: "{app}\\bin\\typeeasy.cmd"; WorkingDir: "{app}"
Name: "{autodesktop}\\TypeEasy"; Filename: "{app}\\bin\\typeeasy.cmd"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; Agrega {app}\bin al PATH del usuario para que `typeeasy hola.te` funcione desde cualquier consola.
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}\\bin"; \
    Check: NeedsAddPath(ExpandConstant('{app}\\bin')); Tasks: addtopath

[Run]
Filename: "{cmd}"; Parameters: "/k cd /d ""{app}"" && .\\bin\\typeeasy.cmd --help"; Description: "Abrir TypeEasy en consola"; Flags: postinstall nowait skipifsilent
Filename: "{cmd}"; Parameters: "/k cd /d ""{app}"" && .\\bin\\typeeasy.cmd --api .\\examples\\endpoint.te --port 9000"; Description: "Probar servidor HTTP (--api endpoint.te en :9000)"; Flags: postinstall nowait skipifsilent unchecked

[Code]
function NeedsAddPath(Param: string): Boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Lowercase(Param) + ';', ';' + Lowercase(OrigPath) + ';') = 0;
end;
