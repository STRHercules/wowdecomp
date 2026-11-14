"""Resolve supplied backpack icons without fuzzy matching or cross-variant substitutions."""
import hashlib
from pathlib import Path
import re
import struct

from PIL import Image

ICON_ROOT = Path("G:/FortnitePorter/Exported/Icons/Backpack Icons")
ALIASES = {
    "F_MED_DistantEchoCastle_Pack": "T-Icon-Backpacks-DistantEchoCastlBackpack.png",
    "F_MED_Journey_Mentor_Shield_Pack": "T-Icon-Backpacks-975-JourneyMentor.png",
    "F_MED_Keen_Pack": "T-Icon-Backpacks-936-KeenFemale.png",
    "F_MED_Mockingbird_Pack": "T-Icon-Backpacks-994-MockingbirdFemale.png",
    "F_MED_Pink_Widow_Pack": "T-Icon-Backpacks-A-025-PinkWidowFemale.png",
    "F_MED_Rays_Pack": "T-Icon-Backpacks-A-039-RaysFemale.png",
    "F_MED_Stamina_Pack": "T-Icon-Backpacks-A-056-StaminaFemale.png",
    "F_MED_Vampire_Hunter_Pack": "T-Icon-Backpacks-981-VampireHunterFemale.png",
    "M_MED_Carbide_Knight_Pack": "T-Icon-Backpacks-A-006-CarbideKnightMale.png",
    "M_MED_CombCrater_Pack": "T_Icon_Backpacks_CombCrater_ReactiveON.png",
    "M_MED_Grandeur_Pack": "T-Icon-Backpacks-906-GrandeurMale.png",
    "M_MED_InchEscape_Pack": "T_Icon_Backpack_InchEscape_L.png",
    "M_MED_Keen_Pack": "T-Icon-Backpacks-937-KeenMale.png",
    "M_MED_Kitty_Warrior_Light_Pack": "T-Icon-Backpacks-931-KittyWarrior.png",
    "M_MED_Stamina_Pack": "T-Icon-Backpacks-A-055-StaminaMale.png",
    "M_MED_Stamina_StandAlone_Pack": "T-Icon-Backpacks-A-058-StaminaMale-StandAlone.png",
    "M_MED_StaminaCat_Pack": "T-Icon-Backpacks-A-057-StaminaCatMale.png",
    "M_MED_Zest_Pack": "T-Icon-Backpacks-948-ZestMale.png",
    "Backpack_60S_Hippie": "T-Icon-Backpacks-HippieFemale.png",
    "F_MED_Cyber_Karate_Pack": "T-Icon-Backpacks-308-CyberKarateFemale.png",
    "F_MED_Dieselpunk_Backpack": "T-Icon-Backpacks-F-MED-Dieselpunk-Backpack.png",
    "F_MED_Medic": "T-Icon-Backpacks-110-MedicFemale.png",
    "F_MED_Snow_Board_Backpack": "T-Icon-Backpacks-161-SnowBoardFemale.png",
    "SK_F_MED_Street_Racer_White": "T-Icon-Backpacks-BID-089-StreetRacerWhiteFemale.png",
    "M_MED_80s_Exercise": "T-Icon-Backpacks-087-ExerciseMale.png",
    "Backpack_Male_Dieselpunk01": "T-Icon-Backpacks-M-MED-Dieselpunk.png",
    "M_MED_Hacivat_Backpack_01": "T-Icon-Backpacks-108-HacivatMale.png",
    "M_MED_Medic_Backpack": "T-Icon-Backpacks-109-MedicMale.png",
    "M_MED_Commando_Werewolf_Backpack": "T-Icon-Backpacks-120-Werewolf.png",
    "Female_Commando_07": "T-Icon-Backpacks-Female-Commando-07-ATH.png",
    "Male_Commando_02": "T-Icon-Backpacks-Male-Commando-02-ATH.png",
    "M_MED_Biker_01_Backpack": "T-Icon-Backpacks-BikerMale.png",
    "Male_Commando_UR01": "T-Icon-Backpacks-Male-Commando-UR01-Backpack-ATH.png",
}
# These original models are not the later Heist llama charm represented by icon 235.
MISSING = {"SK_F_MED_Heist_Backpack", "Backpack_Male_Commando_Heist_Briefcase"}


def key(value):
    value = re.sub(r"^t[-_]icon[-_]backpacks?[-_](?:bid[-_])?(?:a[-_])?(?:[0-9]+[-_])?", "", value.lower())
    value = re.sub("[^a-z0-9]", "", value)
    value = re.sub(r"^sk", "", value)
    value = re.sub(r"^[fm]med", "", value)
    value = re.sub(r"^backpack", "", value)
    return re.sub(r"(backpack|pack)+$", "", value)


def resolve(row, root=ICON_ROOT):
    stem = Path(row["source"]).stem
    if stem in MISSING:
        return None, "No corresponding original Heist icon identified in supplied exports; misleading icon 235 rejected"
    if stem in ALIASES:
        path = root / ALIASES[stem]
        if not path.is_file():
            raise FileNotFoundError(path)
        return path, "Explicit source package, gender and style alias"
    matches = [p for p in root.glob("*.png") if "backpack" in p.name.lower() and key(p.stem) == key(stem)]
    if len(matches) != 1:
        raise ValueError(f"{stem}: expected one exact normalized icon name; found {[p.name for p in matches]}")
    return matches[0], "Exact normalized source mesh/package name"


def icon_path(display):
    return f"Interface\\Icons\\EsteriaBackpack-{display}"


def blp_bytes(path):
    image = Image.open(path).convert("RGBA")
    if image.width & (image.width-1) or image.height & (image.height-1) or max(image.size)>4096:
        raise ValueError("Icon must use supported power-of-two dimensions")
    blob = bytearray(1172)
    struct.pack_into("<4sI4BII", blob, 0, b"BLP2", 1, 3, 8, 8, 1, *image.size)
    for mip in range(16):
        data=image.tobytes("raw","BGRA")
        struct.pack_into("<I",blob,20+4*mip,len(blob))
        struct.pack_into("<I",blob,84+4*mip,len(data))
        blob.extend(data)
        if image.size==(1,1): break
        image=image.resize((max(1,image.width//2),max(1,image.height//2)),Image.Resampling.BOX)
    return bytes(blob)


def entries(rows, ids):
    files, report = {}, []
    for row in rows:
        source, reason = resolve(row)
        assigned=ids[row["slug"]]
        record={"slug":row["slug"],"name":row["name"],**assigned,"reason":reason,"source":str(source) if source else None}
        if source:
            path=icon_path(assigned["display"])
            data=blp_bytes(source)
            files[path+".blp"]=data
            record.update(icon=path,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                          blp_sha256=hashlib.sha256(data).hexdigest())
        report.append(record)
    return files,report
