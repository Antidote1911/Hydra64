#!/usr/bin/env python3
"""Traduction française des textes d'ADS (menus des onglets de panneaux).

Mise à jour : lupdate build/_deps/qtads-src/src -ts translations/ads_fr.ts \\
    -source-language en_US -target-language fr_FR -no-obsolete -locations none
puis : python3 translations/fill_ads_fr.py
"""
import sys
import xml.etree.ElementTree as ET

FR = {
    "Bottom": "En bas",
    "Close": "Fermer",
    "Close Active Tab": "Fermer l'onglet actif",
    "Close Group": "Fermer le groupe",
    "Close Other Groups": "Fermer les autres groupes",
    "Close Others": "Fermer les autres",
    "Close Tab": "Fermer l'onglet",
    "Detach": "Détacher",
    "Detach Group": "Détacher le groupe",
    "Left": "À gauche",
    "List All Tabs": "Liste des onglets",
    "Minimize": "Réduire",
    "Pin": "Épingler",
    "Pin Active Tab (Press Ctrl to Pin Group)": "Épingler l'onglet actif (Ctrl : tout le groupe)",
    "Pin Group": "Épingler le groupe",
    "Pin Group To...": "Épingler le groupe…",
    "Pin To...": "Épingler…",
    "Right": "À droite",
    "Show View": "Afficher le panneau",
    "Top": "En haut",
    "Unpin (Dock)": "Désépingler (ancrer)",
}

def main(path="translations/ads_fr.ts"):
    tree = ET.parse(path)
    missing = []
    for msg in tree.getroot().iter("message"):
        src = msg.find("source").text
        tr = msg.find("translation")
        if src not in FR:
            missing.append(src)
            continue
        tr.attrib.pop("type", None)
        tr.text = FR[src]
    tree.write(path, encoding="utf-8", xml_declaration=True)
    for m in sorted(set(missing)):
        print("non traduit :", repr(m))
    print(f"{path} : {len(missing)} texte(s) sans traduction")
    return 1 if missing else 0

if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
