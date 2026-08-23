"""Build the project-owned four-layer runtime terrain material.

Run through UnrealEditor-Cmd. The source textures are part of the already installed
Fantastic Village pack; this script creates no downloaded or generated artwork.
"""

import unreal


ASSET_FOLDER = "/Game/WorldDirector/Materials"
TERRAIN_ASSET_NAME = "M_WorldDirectorTerrainBlend"
PAVING_ASSET_NAME = "M_WorldDirectorPaving"

LAYERS = (
    (
        "Grass",
        "/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_grass_01_BC",
        "/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_grass_01_N",
        "R",
        0.62,
        (0.55, 0.64, 0.48),
    ),
    (
        "Gravel",
        "/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_gravel_BC",
        "/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_gravel_N",
        "G",
        0.52,
        (0.78, 0.69, 0.58),
    ),
    (
        "Farm",
        "/Game/Fantastic_Village_Pack/textures/T_ENV_farmfield_BC",
        "/Game/Fantastic_Village_Pack/textures/T_ENV_farmfield_N",
        "B",
        0.75,
        (0.68, 0.58, 0.43),
    ),
    (
        "Rock",
        "/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_gravel_02_BC",
        "/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_gravel_02_N",
        "A",
        0.55,
        (0.70, 0.72, 0.72),
    ),
)


def expression(material, expression_class, x, y):
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, expression_class, node_pos_x=x, node_pos_y=y
    )
    if node is None:
        raise RuntimeError(f"Could not create {expression_class}")
    return node


def connect(source, output_name, target, input_name):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        source, output_name, target, input_name
    ):
        raise RuntimeError(
            f"Could not connect {source.get_name()}:{output_name} to "
            f"{target.get_name()}:{input_name}"
        )


def add_pair(material, left, right, x, y):
    node = expression(material, unreal.MaterialExpressionAdd, x, y)
    connect(left, "", node, "A")
    connect(right, "", node, "B")
    return node


def weighted_texture(
    material, vertex_color, name, texture_path, channel, uv_scale, tint, y, normal
):
    texture = unreal.load_asset(texture_path)
    if texture is None:
        raise RuntimeError(f"Missing installed terrain texture: {texture_path}")
    sample = expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -1100, y
    )
    sample.set_editor_property("parameter_name", f"{name} {'Normal' if normal else 'Base Color'}")
    sample.set_editor_property("texture", texture)
    sample.set_editor_property("group", "Terrain Layers")
    coordinates = expression(
        material, unreal.MaterialExpressionTextureCoordinate, -1370, y
    )
    coordinates.set_editor_property("u_tiling", uv_scale)
    coordinates.set_editor_property("v_tiling", uv_scale)
    connect(coordinates, "", sample, "UVs")
    if normal:
        sample.set_editor_property(
            "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
        )
    weighted_source = sample
    if not normal:
        tint_node = expression(material, unreal.MaterialExpressionConstant3Vector, -900, y + 70)
        tint_node.set_editor_property("constant", unreal.LinearColor(*tint, 1.0))
        tinted = expression(material, unreal.MaterialExpressionMultiply, -760, y)
        connect(sample, "RGB", tinted, "A")
        connect(tint_node, "", tinted, "B")
        weighted_source = tinted
    multiply = expression(material, unreal.MaterialExpressionMultiply, -540, y)
    connect(weighted_source, "" if not normal else "RGB", multiply, "A")
    connect(vertex_color, channel, multiply, "B")
    return multiply


def load_or_reset_material(asset_name):
    unreal.EditorAssetLibrary.make_directory(ASSET_FOLDER)
    object_path = f"{ASSET_FOLDER}/{asset_name}"
    material = unreal.load_asset(object_path)
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name,
            ASSET_FOLDER,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
    if material is None:
        raise RuntimeError(f"Could not create {object_path}")

    for existing in list(
        unreal.MaterialEditingLibrary.get_material_expressions(material)
    ):
        unreal.MaterialEditingLibrary.delete_material_expression(material, existing)
    return material, object_path


def compile_and_save(material, object_path, label):
    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    compile_errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if compile_errors:
        raise RuntimeError(f"{label} material failed to compile:\n" + "\n".join(compile_errors))
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {object_path}")


def build_terrain():
    material, object_path = load_or_reset_material(TERRAIN_ASSET_NAME)

    vertex_color = expression(material, unreal.MaterialExpressionVertexColor, -1400, 0)
    base_layers = []
    normal_layers = []
    for index, (name, base_path, normal_path, channel, uv_scale, tint) in enumerate(LAYERS):
        layer_y = -520 + index * 300
        base_layers.append(
            weighted_texture(
                material, vertex_color, name, base_path, channel, uv_scale, tint, layer_y, False
            )
        )
        normal_layers.append(
            weighted_texture(
                material,
                vertex_color,
                name,
                normal_path,
                channel,
                uv_scale,
                tint,
                layer_y + 140,
                True,
            )
        )

    base_a = add_pair(material, base_layers[0], base_layers[1], -430, -260)
    base_b = add_pair(material, base_layers[2], base_layers[3], -430, 340)
    base_sum = add_pair(material, base_a, base_b, -120, 20)

    macro_texture = unreal.load_asset(
        "/Game/Fishermans_Cabin/Textures/Tiling_Textures/Noise/T_Noise_02"
    )
    if macro_texture is None:
        raise RuntimeError("Missing installed macro breakup texture T_Noise_02")
    macro_uv = expression(material, unreal.MaterialExpressionTextureCoordinate, -420, -620)
    macro_uv.set_editor_property("u_tiling", 0.045)
    macro_uv.set_editor_property("v_tiling", 0.045)
    macro_sample = expression(material, unreal.MaterialExpressionTextureSample, -180, -620)
    macro_sample.set_editor_property("texture", macro_texture)
    connect(macro_uv, "", macro_sample, "UVs")
    macro_strength = expression(material, unreal.MaterialExpressionMultiply, 40, -570)
    macro_strength.set_editor_property("const_b", 0.18)
    connect(macro_sample, "R", macro_strength, "A")
    macro_floor = expression(material, unreal.MaterialExpressionAdd, 240, -500)
    macro_floor.set_editor_property("const_b", 0.82)
    connect(macro_strength, "", macro_floor, "A")
    macro_base = expression(material, unreal.MaterialExpressionMultiply, 430, -110)
    connect(base_sum, "", macro_base, "A")
    connect(macro_floor, "", macro_base, "B")

    normal_a = add_pair(material, normal_layers[0], normal_layers[1], -430, -110)
    normal_b = add_pair(material, normal_layers[2], normal_layers[3], -430, 490)
    normal_sum = add_pair(material, normal_a, normal_b, -120, 180)
    normalize = expression(material, unreal.MaterialExpressionNormalize, 100, 180)
    connect(normal_sum, "", normalize, "VectorInput")

    roughness = expression(material, unreal.MaterialExpressionConstant, 100, 360)
    roughness.set_editor_property("r", 0.9)

    if not unreal.MaterialEditingLibrary.connect_material_property(
        macro_base, "", unreal.MaterialProperty.MP_BASE_COLOR
    ):
        raise RuntimeError("Could not connect terrain base color")
    if not unreal.MaterialEditingLibrary.connect_material_property(
        normalize, "", unreal.MaterialProperty.MP_NORMAL
    ):
        raise RuntimeError("Could not connect terrain normal")
    if not unreal.MaterialEditingLibrary.connect_material_property(
        roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
    ):
        raise RuntimeError("Could not connect terrain roughness")

    compile_and_save(material, object_path, "Terrain")
    unreal.log(f"WORLD_DIRECTOR_TERRAIN_MATERIAL_RESULT=PASS asset={object_path}")


def build_paving():
    material, object_path = load_or_reset_material(PAVING_ASSET_NAME)
    base_texture = unreal.load_asset(
        "/Game/Fantastic_Village_Pack/textures/T_ENV_pavingstone_01_BC"
    )
    normal_texture = unreal.load_asset(
        "/Game/Fantastic_Village_Pack/textures/T_ENV_pavingstone_01_N"
    )
    grass_base_texture = unreal.load_asset(
        "/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_grass_01_BC"
    )
    grass_normal_texture = unreal.load_asset(
        "/Game/Fantastic_Village_Pack/textures/T_ENV_TERRAIN_grass_01_N"
    )
    noise_texture = unreal.load_asset(
        "/Game/Fishermans_Cabin/Textures/Tiling_Textures/Noise/T_Noise_02"
    )
    if (
        base_texture is None
        or normal_texture is None
        or grass_base_texture is None
        or grass_normal_texture is None
        or noise_texture is None
    ):
        raise RuntimeError("Missing installed paving or macro-breakup texture")

    coordinates = expression(material, unreal.MaterialExpressionTextureCoordinate, -1000, -180)
    coordinates.set_editor_property("u_tiling", 0.65)
    coordinates.set_editor_property("v_tiling", 0.65)
    base = expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -760, -260)
    base.set_editor_property("parameter_name", "Paving Base Color")
    base.set_editor_property("texture", base_texture)
    base.set_editor_property("group", "Paving")
    connect(coordinates, "", base, "UVs")
    normal = expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -760, 80)
    normal.set_editor_property("parameter_name", "Paving Normal")
    normal.set_editor_property("texture", normal_texture)
    normal.set_editor_property("group", "Paving")
    normal.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    connect(coordinates, "", normal, "UVs")

    grass_coordinates = expression(material, unreal.MaterialExpressionTextureCoordinate, -1000, 250)
    grass_coordinates.set_editor_property("u_tiling", 0.62)
    grass_coordinates.set_editor_property("v_tiling", 0.62)
    grass_base = expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -760, 230)
    grass_base.set_editor_property("parameter_name", "Edge Grass Base Color")
    grass_base.set_editor_property("texture", grass_base_texture)
    grass_base.set_editor_property("group", "Paving Edge")
    connect(grass_coordinates, "", grass_base, "UVs")
    grass_normal = expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -760, 430)
    grass_normal.set_editor_property("parameter_name", "Edge Grass Normal")
    grass_normal.set_editor_property("texture", grass_normal_texture)
    grass_normal.set_editor_property("group", "Paving Edge")
    grass_normal.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    connect(grass_coordinates, "", grass_normal, "UVs")

    tint = expression(material, unreal.MaterialExpressionConstant3Vector, -530, -100)
    tint.set_editor_property("constant", unreal.LinearColor(0.72, 0.74, 0.72, 1.0))
    tinted = expression(material, unreal.MaterialExpressionMultiply, -300, -230)
    connect(base, "RGB", tinted, "A")
    connect(tint, "", tinted, "B")

    macro_coordinates = expression(material, unreal.MaterialExpressionTextureCoordinate, -760, -560)
    macro_coordinates.set_editor_property("u_tiling", 0.055)
    macro_coordinates.set_editor_property("v_tiling", 0.055)
    macro = expression(material, unreal.MaterialExpressionTextureSample, -520, -560)
    macro.set_editor_property("texture", noise_texture)
    connect(macro_coordinates, "", macro, "UVs")
    macro_strength = expression(material, unreal.MaterialExpressionMultiply, -300, -520)
    macro_strength.set_editor_property("const_b", 0.1)
    connect(macro, "R", macro_strength, "A")
    macro_floor = expression(material, unreal.MaterialExpressionAdd, -80, -470)
    macro_floor.set_editor_property("const_b", 0.9)
    connect(macro_strength, "", macro_floor, "A")
    broken_up = expression(material, unreal.MaterialExpressionMultiply, 150, -180)
    connect(tinted, "", broken_up, "A")
    connect(macro_floor, "", broken_up, "B")

    grass_tint = expression(material, unreal.MaterialExpressionConstant3Vector, -300, 300)
    grass_tint.set_editor_property("constant", unreal.LinearColor(0.64, 0.72, 0.53, 1.0))
    tinted_grass = expression(material, unreal.MaterialExpressionMultiply, -80, 260)
    connect(grass_base, "RGB", tinted_grass, "A")
    connect(grass_tint, "", tinted_grass, "B")
    broken_grass = expression(material, unreal.MaterialExpressionMultiply, 150, 250)
    connect(tinted_grass, "", broken_grass, "A")
    connect(macro_floor, "", broken_grass, "B")

    vertex_color = expression(material, unreal.MaterialExpressionVertexColor, 120, 30)
    blended_base = expression(material, unreal.MaterialExpressionLinearInterpolate, 390, -100)
    connect(broken_grass, "", blended_base, "A")
    connect(broken_up, "", blended_base, "B")
    connect(vertex_color, "R", blended_base, "Alpha")
    blended_normal = expression(material, unreal.MaterialExpressionLinearInterpolate, 390, 160)
    connect(grass_normal, "RGB", blended_normal, "A")
    connect(normal, "RGB", blended_normal, "B")
    connect(vertex_color, "R", blended_normal, "Alpha")
    normalized_blended_normal = expression(material, unreal.MaterialExpressionNormalize, 610, 160)
    connect(blended_normal, "", normalized_blended_normal, "VectorInput")
    roughness = expression(material, unreal.MaterialExpressionConstant, 610, 340)
    roughness.set_editor_property("r", 0.88)

    if not unreal.MaterialEditingLibrary.connect_material_property(
        blended_base, "", unreal.MaterialProperty.MP_BASE_COLOR
    ):
        raise RuntimeError("Could not connect paving base color")
    if not unreal.MaterialEditingLibrary.connect_material_property(
        normalized_blended_normal, "", unreal.MaterialProperty.MP_NORMAL
    ):
        raise RuntimeError("Could not connect paving normal")
    if not unreal.MaterialEditingLibrary.connect_material_property(
        roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
    ):
        raise RuntimeError("Could not connect paving roughness")

    compile_and_save(material, object_path, "Paving")
    unreal.log(f"WORLD_DIRECTOR_PAVING_MATERIAL_RESULT=PASS asset={object_path}")


build_terrain()
build_paving()
