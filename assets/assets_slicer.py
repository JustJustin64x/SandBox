import os
from PIL import Image

# ==========================================
# KONFIGURATION ALLER ASSET-JOBS
# ==========================================
# Hier kannst du jedes generierte Bild eintragen.
# input: Wie das Bild von der KI heißt, das im Ordner liegt.
# cols/rows: Wie viele Spalten und Zeilen das KI-Bild hat.
# size: Die Zielauflösung (z.B. 16x16 oder 32x32).
# mode: "individual" (speichert jede Kachel einzeln) oder "spritesheet" (setzt sie nahtlos zusammen).
# make_transparent: Wandelt den weißen KI-Hintergrund in unsichtbar (Alpha) um.

TASKS = [
    # 1. TERRAIN (3x3 Grid -> 9 Einzelbilder)
    {
        "input": "env.png",
        "out_dir": "assets/terrain",
        "cols": 3, "rows": 3,
        "size": (16, 16),
        "remove_borders": True,
        "make_transparent": False, # Terrain braucht meist keine Transparenz
        "mode": "individual",
        "filenames": [
            "water_deep.png", "water_shallow.png", "sand.png",
            "grass_1.png", "grass_3.png", "forest.png",
            "mountain_base.png", "mountain_cliff.png", "snow.png"
        ]
    },
    
    # 2. BEISPIEL: GEBÄUDE (z.B. ein 3x2 Grid von der KI generiert)
    {
        "input": "buildings_stoneage.png",
        "out_dir": "assets/buildings",
        "cols": 3, "rows": 2,
        "size": (32, 32),
        "remove_borders": True,
        "make_transparent": True, # Gebäude brauchen einen transparenten Hintergrund!
        "mode": "individual",
        "filenames": [
            "towncenter_tier1.png", "hut_tier1.png", "pantry.png",
            "craftbench.png", "monument.png", "grave.png"
        ]
    },

    # 3. BEISPIEL: CHARAKTER-ANIMATIONEN (4x4 Grid -> 1 Sprite-Sheet)
    {
        "input": "caveman_male_ai.png",
        "out_dir": "assets/entities",
        "cols": 4, "rows": 4,
        "size": (32, 32), # Größe EINES Frames
        "remove_borders": True,
        "make_transparent": True,
        "mode": "spritesheet", # Fügt die Bilder nahtlos zu einem 128x128 Bild zusammen!
        "filenames": ["human_male_spritesheet.png"]
    }
]

# ==========================================
# LOGIK
# ==========================================

def remove_white_background(img):
    """Wandelt weiße (oder fast weiße) Pixel in Transparenz um."""
    img = img.convert("RGBA")
    datas = img.getdata()
    new_data = []
    for item in datas:
        # Finde weiße oder fast weiße Pixel (Toleranzwert)
        if item[0] > 235 and item[1] > 235 and item[2] > 235:
            new_data.append((255, 255, 255, 0)) # Transparent
        else:
            new_data.append(item)
    img.putdata(new_data)
    return img

def process_image(task):
    input_path = task["input"]
    if not os.path.exists(input_path):
        print(f"[-] Überspringe Task: '{input_path}' wurde nicht im Ordner gefunden.")
        return

    print(f"[+] Verarbeite '{input_path}'...")
    img = Image.open(input_path).convert("RGBA")
    img_w, img_h = img.size
    cols, rows = task["cols"], task["rows"]
    cell_w, cell_h = img_w / cols, img_h / rows
    
    target_w, target_h = task["size"]
    extracted_frames = []

    for i in range(rows):
        for j in range(cols):
            # Grober Zellenschnitt
            left, top = j * cell_w, i * cell_h
            right, bottom = (j + 1) * cell_w, (i + 1) * cell_h
            cell = img.crop((left, top, right, bottom))
            
            # KI-Ränder (Texte/Rahmen) wegschneiden
            if task.get("remove_borders", True):
                pad_x, pad_y = cell_w * 0.05, cell_h * 0.05
                sq_size = min(cell_w - 2*pad_x, cell_h - 2*pad_y)
                c_x, c_y = cell_w / 2, cell_h / 2
                cell = cell.crop((c_x - sq_size/2, c_y - sq_size/2, c_x + sq_size/2, c_y + sq_size/2))
            
            # Transparenz anwenden
            if task.get("make_transparent", False):
                cell = remove_white_background(cell)
            
            # Pixel-Perfekt skalieren (Nearest Neighbor)
            cell = cell.resize((target_w, target_h), Image.NEAREST)
            extracted_frames.append(cell)

    os.makedirs(task["out_dir"], exist_ok=True)

    if task.get("mode") == "spritesheet":
        # Einzelbilder nahtlos zusammenfügen
        sheet_w = cols * target_w
        sheet_h = rows * target_h
        sheet = Image.new("RGBA", (sheet_w, sheet_h), (0, 0, 0, 0))
        
        for idx, frame in enumerate(extracted_frames):
            x = (idx % cols) * target_w
            y = (idx // cols) * target_h
            sheet.paste(frame, (x, y))
            
        out_path = os.path.join(task["out_dir"], task["filenames"][0])
        sheet.save(out_path)
        print(f"   -> Spritesheet erstellt: {out_path}")
        
    else:
        # Einzelbilder abspeichern
        for idx, frame in enumerate(extracted_frames):
            if idx < len(task["filenames"]):
                out_path = os.path.join(task["out_dir"], task["filenames"][idx])
                frame.save(out_path)
                print(f"   -> Erstellt: {out_path}")

print("=== Asset-Slicer gestartet ===")
for task in TASKS:
    process_image(task)
print("=== Alle Aufgaben erledigt ===")
