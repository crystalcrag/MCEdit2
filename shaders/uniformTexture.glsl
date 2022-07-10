/*
 * uniformTexture.glsl: textures are all bound at fixed location for easy sharing between all shaders.
 *
 * written by T.Pierron, june 2022
 */

layout (binding=0) uniform sampler2D blockTex;      // main texture for blocks (512 x 1024 x RGBA)
layout (binding=1) uniform sampler2D entitiesTex;   // entity models (512 x 1024 x RGBA)
layout (binding=2) uniform sampler2D tint;          // color of the sky on the half-sphere where the sun is (time x height x RGB)
layout (binding=3) uniform sampler2D tint2;         // color of the sky on the opposite half-sphere (time x height x RGB)
layout (binding=4) uniform sampler2D sun;           // sun color (radius x time of day x RGBA)
layout (binding=5) uniform sampler2D lightShadeTex; // skylight + blocklight per face shading (16 x 108 x RGB)
layout (binding=6) uniform sampler2D skyTex;        // texture for blending terrain with sky (256 x 256 x RGB)

// lighting banks for lighting value (144 x 144 x 144 x RG: R = skylight, G = blockLight)
layout (binding=8)  uniform sampler3D lightBank0;
layout (binding=9)  uniform sampler3D lightBank1;
layout (binding=10) uniform sampler3D lightBank2;
layout (binding=11) uniform sampler3D lightBank3;
layout (binding=12) uniform sampler3D lightBank4;
layout (binding=13) uniform sampler3D lightBank5;
layout (binding=14) uniform sampler3D lightBank6;
layout (binding=15) uniform sampler3D lightBank7;
layout (binding=16) uniform sampler3D lightBank8;
layout (binding=17) uniform sampler3D lightBank9;
layout (binding=18) uniform sampler3D lightBank10;
layout (binding=19) uniform sampler3D lightBank11;
layout (binding=20) uniform sampler3D lightBank12;
layout (binding=21) uniform sampler3D lightBank13;
layout (binding=22) uniform sampler3D lightBank14;
layout (binding=23) uniform sampler3D lightBank15;
