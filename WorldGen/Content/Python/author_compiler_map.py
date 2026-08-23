import unreal


result = unreal.WorldGenAssetAuthoringLibrary.author_compiler_town_map()
if not result:
    raise RuntimeError("World Director compiler map authoring failed")
unreal.log("WORLD_DIRECTOR_AUTHOR_MAP_RESULT=PASS")
