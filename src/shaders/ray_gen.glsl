#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "../shared_with_shaders.h"
#include "../shaders/random.glsl"

layout(set = SWS_SCENE_AS_SET,     binding = SWS_SCENE_AS_BINDING)            uniform accelerationStructureNV Scene;
layout(set = SWS_RESULT_IMAGE_SET, binding = SWS_RESULT_IMAGE_BINDING, rgba8) uniform image2D ResultImage;

layout(set = SWS_CAMDATA_SET,      binding = SWS_CAMDATA_BINDING, std140)     uniform AppData {
    UniformParams Params;
};

layout(location = SWS_LOC_PRIMARY_RAY) rayPayloadNV RayPayload PrimaryRay;
layout(location = SWS_LOC_SHADOW_RAY)  rayPayloadNV ShadowRayPayload ShadowRay;

const float kBunnyRefractionIndex = 1.0f / 1.31f; // ice
float Schlick(const float cosine, const float refractionIndex)
{
	float r0 = (1 - refractionIndex) / (1 + refractionIndex);
	r0 *= r0;
	return r0 + (1 - r0) * pow(1 - cosine, 5);
}

vec3 CalcRayDir(vec2 screenUV, float aspect) {
    vec3 u = Params.camSide.xyz;
    vec3 v = Params.camUp.xyz;

	const float planeWidth = tan(Params.camNearFarFov.z * 0.5f);

	u *= (planeWidth * aspect);
	v *= planeWidth;

	const vec3 rayDir = normalize(Params.camDir.xyz + (u * screenUV.x) - (v * screenUV.y));
	return rayDir;
}

void main() {
	uint pixelRandomSeed = InitRandomSeed(gl_LaunchIDNV.x, gl_LaunchIDNV.y);
    const float aspect = float(gl_LaunchSizeNV.x) / float(gl_LaunchSizeNV.y);
	const vec3 LightSource = vec3(0.0f, 0.0f, 0.0f);
       
    const uint rayFlags = gl_RayFlagsOpaqueNV;
    const uint shadowRayFlags = gl_RayFlagsOpaqueNV | gl_RayFlagsTerminateOnFirstHitNV;

    const uint cullMask = 0xFF;

    const uint stbRecordStride = 1;

    const float tmin = 0.0001f;
	const float tmax = Params.camNearFarFov.y * 0.75f;
	
	vec2 pixel = gl_LaunchIDNV.xy;

    vec3 finalColor = vec3(0.0f);
	for(int t = 0; t < SWS_MAX_RAYS; ++t){
		const vec2 curPixel = vec2(pixel.x + RandomFloat(pixelRandomSeed), pixel.y + RandomFloat(pixelRandomSeed));
		const vec2 uv = (curPixel / gl_LaunchSizeNV.xy) * 2.0f - 1.0f;

		vec3 origin = Params.camPos.xyz;
		vec3 direction = CalcRayDir(uv, aspect);

		for (int i = 0; i < SWS_MAX_RECURSION; ++i) {

			traceNV(Scene, rayFlags, cullMask, SWS_PRIMARY_HIT_SHADERS_IDX, stbRecordStride, SWS_PRIMARY_MISS_SHADERS_IDX, origin, tmin, direction, tmax, SWS_LOC_PRIMARY_RAY);

			const vec3 hitColor = PrimaryRay.colorAndDist.rgb;
			const float hitDistance = PrimaryRay.colorAndDist.w;
			const float objectId = PrimaryRay.normalAndObjId.w;
			const vec3 normal = PrimaryRay.normalAndObjId.xyz;

			// if hit background - quit
			if (hitDistance < 0.0f) {
				finalColor += hitColor;
				break;
			}
			else {
				const vec3 hitNormal = PrimaryRay.normalAndObjId.xyz;
				const vec3 hitPos = origin + direction * hitDistance;
				vec3 toLight = abs(normalize(hitPos - LightSource));

				if (objectId == OBJECT_ID_BOX1) {
					const vec3 shadowRayOrigin = hitPos + hitNormal * 0.001f;
					traceNV(Scene, shadowRayFlags, cullMask, SWS_SHADOW_HIT_SHADERS_IDX, stbRecordStride, SWS_SHADOW_MISS_SHADERS_IDX, shadowRayOrigin, 0.0f, toLight, tmax, SWS_LOC_SHADOW_RAY);
					const float lighting = (ShadowRay.distance > 0.0f) ? Params.sunPosAndAmbient.w : max(Params.sunPosAndAmbient.w, dot(hitNormal, toLight));
					finalColor += hitColor * lighting;
					origin = hitPos + 0.001f * direction;
					direction = vec3(normal + RandomInUnitSphere(pixelRandomSeed));

				}
				else if (objectId == OBJECT_ID_BOX2) { //ROOM
					origin = hitPos + 0.001f * direction;					
					direction = vec3(hitNormal + RandomInUnitSphere(pixelRandomSeed));
				}
				else if (objectId == OBJECT_ID_BOX3) {
					const float dot = dot(direction, normal);
					const vec3 outwardDormal = dot > 0 ? -normal : normal;
					const float niOverNt = dot > 0 ? kBunnyRefractionIndex : 1 / kBunnyRefractionIndex;
					const float cosine = dot > 0 ? kBunnyRefractionIndex * dot : -dot;
					const vec3 refracted = refract(direction, outwardDormal, niOverNt);
					const float reflectProb = refracted != vec3(0) ? Schlick(cosine, kBunnyRefractionIndex) : 1;
					const vec3 scatter = RandomFloat(pixelRandomSeed) < reflectProb ? reflect(direction, normal) : refracted;
					origin = hitPos + 0.001f * direction;
					direction = vec3(scatter);				
				}
				else if (objectId == OBJECT_ID_LIGHT_PLANE) {
					origin = hitPos + 0.001f * direction;
					direction = vec3(1,0,0);
					finalColor += vec3(1.0f, 1.0f, 1.0f);
					break;
				}
				else {
					const vec3 shadowRayOrigin = hitPos + hitNormal * 0.001f;
					//traceNV(Scene, shadowRayFlags, cullMask, SWS_SHADOW_HIT_SHADERS_IDX, stbRecordStride, SWS_SHADOW_MISS_SHADERS_IDX, shadowRayOrigin, 0.0f, toLight, tmax, SWS_LOC_SHADOW_RAY);
					const float lighting = (ShadowRay.distance > 0.0f) ? Params.sunPosAndAmbient.w : max(Params.sunPosAndAmbient.w, dot(hitNormal, toLight));
					finalColor += hitColor * lighting;
					break;
				}
			}
		}
	}
	finalColor = finalColor/ SWS_MAX_RAYS;
    imageStore(ResultImage, ivec2(gl_LaunchIDNV.xy), vec4(LinearToSrgb(finalColor), 1.0f));
}
