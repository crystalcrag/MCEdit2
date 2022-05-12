/*
 * fragment shader for dynamic skydome of MCEdit v2
 *
 * original idea from Simon Rodriguez, https://github.com/kosua20/opengl-skydome
 */
#version 430 core

#include "uniformBlock.glsl"

in vec3 pos;
in vec3 sun_norm;
in vec3 star_pos;

layout (binding=2) uniform sampler2D tint;    // the color of the sky on the half-sphere where the sun is. (time x height)
layout (binding=3) uniform sampler2D tint2;   // the color of the sky on the opposite half-sphere. (time x height)
layout (binding=4) uniform sampler2D sun;

out vec4 fragcol;

uniform float weather = 1.0; // mixing factor (0.5 to 1.0)
uniform float time;
uniform float skyTexOnly;

uniform float cirrus = 0.5;
uniform float cumulus = 0.5;

//---------NOISE GENERATION------------
//Noise generation based on a simple hash, to ensure that if a given point on the dome
//(after taking into account the rotation of the sky) is a star, it remains a star all night long
float hash(float n)
{
	return fract(sin(n) * 43758.5453123);
}
float noise3d(vec3 x)
{
	float xhash = hash(round(400*x.x) * 37.0);
	float yhash = hash(round(400*x.y) * 57.0);
	float zhash = hash(round(400*x.z) * 67.0);
	return fract(xhash + yhash + zhash);
}
float noise(vec3 x)
{
	vec3 f = fract(x);
	float n = dot(floor(x), vec3(1.0, 157.0, 113.0));
	return mix(mix(mix(hash(n +   0.0), hash(n +   1.0), f.x),
	               mix(hash(n + 157.0), hash(n + 158.0), f.x), f.y),
	           mix(mix(hash(n + 113.0), hash(n + 114.0), f.x),
	               mix(hash(n + 270.0), hash(n + 271.0), f.x), f.y), f.z);
}

const mat3 m = mat3(0.0, 1.60,  1.20, -1.6, 0.72, -0.96, -1.2, -0.96, 1.28);
float fbm(vec3 p)
{
	float f = 0.0;
	f += noise(p) / 2; p = m * p * 1.1;
	f += noise(p) / 4; p = m * p * 1.2;
	f += noise(p) / 6; p = m * p * 1.3;
	f += noise(p) / 12; p = m * p * 1.4;
	f += noise(p) / 24;
	return f;
}

#define SUNRADIUS   0.15
#define CORONA      (SUNRADIUS*3)
#define FOG_TEX_SZ  128

void main()
{
	vec3 color;
	vec3 pos_norm = normalize(pos);
	float dist = dot(sun_norm, pos_norm);

	//We read the tint texture according to the position of the sun and the weather factor
	vec3 color_wo_sun = texture(tint2, vec2(min((sun_norm.y + 1.0) / 2.0, 0.99), max(0.01, pos_norm.y))).rgb;
	vec3 color_w_sun  = texture(tint,  vec2(min((sun_norm.y + 1.0) / 2.0, 0.99), max(0.01, pos_norm.y))).rgb;

	color = mix(color_wo_sun, color_w_sun, dist * 0.5 + 0.5);

	if (skyTexOnly == 0)
	{
		// Computing u and v for the clouds textures (spherical projection)
		float u = 0.5 + atan(pos_norm.z, pos_norm.x) / (2 * M_PI);
		float v = -0.5 + asin(pos_norm.y) / M_PI;



		// Cloud color
		// color depending on the weather (shade of grey) *  (day or night)
		vec3 cloud_color = clamp(vec3(min(weather*3.0/2.0,1.0)) + color.xyz * 0.5, 0, 1) * clamp(sun_norm.y > 0.05 ? 0.95 : 0.95 + (sun_norm.y-0.05) * 1.8, 0.075, 1.0);
		float transparency;

		#if 0
		// Reading from the clouds maps
		// mixing according to the weather (1.0 -> clouds1 (sunny), 0.5 -> clouds2 (rainy))
		// + time translation along the u-axis (horizontal) for the clouds movement
		transparency = texture(clouds1, vec2(u+time,v)).r;
		#else
		if (pos_norm.y > 0)
		{
			transparency = smoothstep(1.0 - cirrus, 1.0, fbm(pos_norm.xyz / pos_norm.y * 2.0 + time * 0.3)) +
			               smoothstep(1.0 - cumulus, 1.0, fbm(0.7  * pos_norm.xyz / pos_norm.y + time * 0.3));

			transparency *= clamp((1 / 0.15) * pos_norm.y, 0.0, 1.0);
		}
		else transparency = 0;
		#endif

		// Stars
		if (sun_norm.y < 0.1) // night or dawn
		{
			float threshold = 0.99;
			// we generate a random value between 0 and 1
			float star_intensity = noise3d(normalize(star_pos));
			// and we apply a threshold to keep only the brightest areas
			if (star_intensity >= threshold)
			{
				//We compute the star intensity
				star_intensity = pow((star_intensity - threshold)/(1.0 - threshold), 6.0)*(-sun_norm.y+0.1);
				color += vec3(star_intensity);
			}
		}

		// Sun
		float radius = length(pos_norm-sun_norm);
		if (radius < SUNRADIUS) // we are in the area of the sky which is covered by the sun
		{
			float time = clamp(sun_norm.y,0.1,0.99);
			float normRadius = radius / SUNRADIUS;
			if(normRadius < 1.0-0.01) // we need a small bias to avoid flickering on the border of the texture
			{
				// we read the alpha value from a texture where x = radius and y=height in the sky (~time)
				vec4 sun_color = texture(sun, vec2(normRadius, time));
				color = mix(color, sun_color.rgb, sun_color.a);
			}
		}
		// corona
		if (radius < CORONA)
		{
			float addcol = 1 - radius / CORONA;
			color += addcol * addcol * 0.5 * clamp(sun_norm.y+0.5, 0.0, 1.0);
		}

		#if 0
		//Moon
		float radius_moon = length(pos_norm+sun_norm); // the moon is at position -sun_pos
		if(radius_moon < 0.03) //We are in the area of the sky which is covered by the moon
		{
			//We define a local plane tangent to the skydome at -sun_norm
			//We work in model space (everything normalized)
			vec3 n1 = normalize(cross(-sun_norm,vec3(0,1,0)));
			vec3 n2 = normalize(cross(-sun_norm,n1));
			//We project pos_norm on this plane
			float x = dot(pos_norm,n1);
			float y = dot(pos_norm,n2);
			//x,y are two sine, ranging approx from 0 to sqrt(2)*0.03. We scale them to [-1,1], then we will translate to [0,1]
			float scale = 23.57*0.5;
			//we need a compensation term because we made projection on the plane and not on the real sphere + other approximations.
			float compensation = 1.4;
			//And we read in the texture of the moon. The projection we did previously allows us to have an undeformed moon
			//(for the sun we didn't care as there are no details on it)
			color = mix(color,texture(moon,vec2(x,y)*scale*compensation+vec2(0.5)).rgb,clamp(-sun_norm.y*3,0,1));
		}
		#endif

		// final mix
		// mixing with the cloud color allows us to hide things behind clouds (sun, stars, moon)
		color = mix(color, cloud_color, clamp((2-weather)*transparency,0.0,1.0));
	}

	// horizon tweak
	if (-0.2 <= pos_norm.y && pos_norm.y <= 0.2)
	{
		// somewhat simulate (poorly) the Mie scattering
		float factor = (cos(pos_norm.y * 5*M_PI) + 1) * (0.1 * sun_norm.y * sun_norm.y) + 1;
		color *= factor;
	}
	// cannot attach a second color buffer to the default frame buffer, really ?
	fragcol = vec4(color, 1);
}
