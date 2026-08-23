import unreal


def main():
    library = unreal.WorldGenAssetAuthoringLibrary
    if not library.author_resident_life_state_tree(
        "/Game/WorldDirector/AI/ST_ResidentLife"
    ):
        raise RuntimeError("Failed to author ST_ResidentLife")
    if not library.configure_living_town_map(
        "/Game/WorldDirector/Maps/L_WorldDirectorTown",
        "living-town.json",
    ):
        raise RuntimeError("Failed to configure the living-town map")
    unreal.log("WORLD_DIRECTOR_PHASE4_AUTHORING_RESULT=PASS")


main()
