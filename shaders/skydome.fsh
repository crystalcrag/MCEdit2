/*
 * fragment shader for dynamic skydome of MCEdit v2
 *
 * original idea from Simon Rodriguez, https://github.com/kosua20/opengl-skydome
 */
#version 430 core

in vec3 pos;
in vec3 sun_norm;
in vec3 star_pos;

layout (binding=2) uniform sampler2D tint;    // the color of the sky on the half-sphere where the sun is. (time x height)
layout (binding=3) uniform sampler2D tint2;   // the color of the sky on the opposite half-sphere. (time x height)
layout (binding=4) uniform sampler2D sun;
layout (binding=5) uniform sampler2D clouds1; // light clouds texture (spherical UV projection)

uniform float weather;//mixing factor (0.5 to 1.0)
uniform float time;

out vec4 fragcol;

//---------NOISE GENERATION------------
//Noise generation based on a simple hash, to ensure that if a given point on the dome
//(after taking into account the rotation of the sky) is a star, it remains a star all night long
float Hash(float n)
{
	return fract((1.0 + sin(n)) * 415.92653);
}
float Noise3d(vec3 x)
{
	float xhash = Hash(round(400*x.x) * 37.0);
	float yhash = Hash(round(400*x.y) * 57.0);
	float zhash = Hash(round(400*x.z) * 67.0);
	return fract(xhash + yhash + zhash);
}

#define SUNRADIUS  0.2
#define M_PI       3.14159265

void main()
{
	vec3 color;
	vec3 pos_norm = normalize(pos);
	float dist = dot(sun_norm, pos_norm);

	//We read the tint texture according to the position of the sun and the weather factor
	vec3 color_wo_sun = texture(tint2, vec2(min((sun_norm.y + 1.0) / 2.0, 0.99), max(0.01, pos_norm.y))).rgb;
	vec3 color_w_sun  = texture(tint,  vec2(min((sun_norm.y + 1.0) / 2.0, 0.99), max(0.01, pos_norm.y))).rgb;

	color = mix(color_wo_sun, color_w_sun, dist * 0.5 + 0.5);

	// Computing u and v for the clouds textures (spherical projection)
	float u = 0.5 + atan(pos_norm.z, pos_norm.x) / (2 * M_PI);
	float v = -0.5 + asin(pos_norm.y) / M_PI;

	// Cloud color
	// color depending on the weather (shade of grey) *  (day or night)
	vec3 cloud_color = vec3(min(weather*3.0/2.0,1.0)) * (sun_norm.y > 0 ? 0.95 : 0.95 + sun_norm.y * 1.8);

	// Reading from the clouds maps
	// mixing according to the weather (1.0 -> clouds1 (sunny), 0.5 -> clouds2 (rainy))
	// + time translation along the u-axis (horizontal) for the clouds movement
	float transparency = texture(clouds1, vec2(u+time,v)).r;

	// Stars
	if (sun_norm.y < 0.1) // night or dawn
	{
		float threshold = 0.99;
		// we generate a random value between 0 and 1
		float star_intensity = Noise3d(normalize(star_pos));
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
	if(radius < SUNRADIUS) // we are in the area of the sky which is covered by the sun
	{
		float time = clamp(sun_norm.y,0.1,0.99);
		radius = radius / SUNRADIUS;
		if(radius < 1.0-0.001) // we need a small bias to avoid flickering on the border of the texture
		{
			// we read the alpha value from a texture where x = radius and y=height in the sky (~time)
			vec4 sun_color = texture(sun,vec2(radius,time));
			color = mix(color,sun_color.rgb,sun_color.a);
		}
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
	color = mix(color, cloud_color, clamp((2-weather)*transparency,0,1));

	// horizon tweak
	#if 1
	if (-0.2 <= pos_norm.y && pos_norm.y <= 0.2)
	{
		// somewhat simulate (poorly) the Mie scattering
		float factor = 0.1 * sun_norm.y * sun_norm.y;
		color *= (cos(pos_norm.y * 5*M_PI) + 1) * factor + 1;
	}
	#endif
	fragcol = vec4(color, 1);
}
