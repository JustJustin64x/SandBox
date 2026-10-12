# SandBox

2D-Sandbox in C++17 mit raylib.

## Windows: bauen und starten

Voraussetzungen: Git und ein MinGW-w64-Compiler (`g++`, `gcc`, `ar`) im PATH.
Im Projektordner ausfuehren:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Run
```

Ohne `-Run` wird nur gebaut. Beim ersten Build wird raylib 5.5 von seinem
offiziellen GitHub-Repository nach `build/deps/raylib` heruntergeladen und
lokal kompiliert. Dafuer ist Internetzugriff erforderlich. Weitere Builds
verwenden die lokale Bibliothek. Make ist fuer diesen Windows-Build nicht noetig.

Die erzeugte Anwendung kann aus dem Projektordner mit `./worldbox.exe`
gestartet werden, damit sie den Ordner `assets` findet.

In VS Code den Ordner `SandBox` oeffnen und **Strg+Umschalt+B** zum Bauen
oder **Terminal > Aufgabe ausfuehren > Run WorldBox** zum Starten verwenden.
`main.cpp` allein zu kompilieren reicht nicht: Das Programm braucht die
Include-Verzeichnisse, alle Quelldateien und die raylib-Bibliothek.

Die mitgelieferte `.vscode/c_cpp_properties.json` konfiguriert diese Pfade
auch fuer IntelliSense. Dafuer muss `SandBox` der geoeffnete VS-Code-Projektordner
sein. Die raylib-Header werden beim ersten Build bereitgestellt.

Das Fenster passt sich beim Start an den Monitor an. Beim Vergroessern und
Verkleinern bleiben Spiel und Menues proportional zentriert; andere
Seitenverhaeltnisse erhalten schwarze Raender. F11 oder Alt+Enter schaltet
zwischen Fenster und randlosem Vollbild um.

Der Render-Test prueft Bildpositionen und Mauskoordinaten bei verschiedenen
Fenstergroessen sowie den Vollbildwechsel mit dem vorhandenen Grafiktreiber:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Test
```

Fenstergroessen oberhalb der Monitorgroesse koennen vom Betriebssystem begrenzt
werden; das Testprotokoll nennt die tatsaechlichen Fenster- und Framebuffer-Masse.

## Make

Mit installiertem Make, g++ und raylib bleibt auch `make run` verfuegbar.
