#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../shared_with_shaders.h"
#include "../shaders/random.glsl"

layout(set = SWS_SCENE_AS_SET,     binding = SWS_SCENE_AS_BINDING)            uniform accelerationStructureNV Scene;
layout(set = SWS_RESULT_IMAGE_SET, binding = SWS_RESULT_IMAGE_BINDING, rgba8) uniform image2D ResultImage;

layout(set = SWS_CAMDATA_SET,      binding = SWS_CAMDATA_BINDING, std140)     uniform AppData {
    UniformParams Params;
};

layout(location = SWS_LOC_PRIMARY_RAY) rayPayloadNV RayPayload PrimaryRay;
layout(location = SWS_LOC_SHADOW_RAY)  rayPayloadNV ShadowRayPayload ShadowRay;

const float RefractionIndex = 1.0f / 1.31f; // ice
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
	uint seed = InitRandomSeed(gl_LaunchIDNV.x, gl_LaunchIDNV.y);
    const float aspect = float(gl_LaunchSizeNV.x) / float(gl_LaunchSizeNV.y);
	const vec3 LightSource = vec3(0.0f, 100.0f, 0.0f);
       
    const uint rayFlags = gl_RayFlagsOpaqueNV;
    const uint shadowRayFlags = gl_RayFlagsOpaqueNV | gl_RayFlagsTerminateOnFirstHitNV;

    const uint cullMask = 0xFF;

    const uint stbRecordStride = 0;

    const float tmin = 0.0001f;
	const float tmax = Params.camNearFarFov.y * 0.75f;
	
	
	vec2 curPixel = vec2(gl_LaunchIDNV.x, gl_LaunchIDNV.y);

    vec3 finalColor = vec3(0.0f);
	for(int t = 0; t < SWS_MAX_RAYS; ++t){

		const vec2 uv = (curPixel / gl_LaunchSizeNV.xy) * 2.0f - 1.0f;
		vec3 origin = Params.camPos.xyz;
		vec3 direction = CalcRayDir(uv, aspect);
		vec3 rayColor = vec3(1.0f);
		for (int i = 0; i < SWS_MAX_RECURSION; ++i) {

			traceNV(Scene, rayFlags, cullMask, 0, stbRecordStride, 0, origin, tmin, direction, tmax, 0);

			const vec3 hitColor = PrimaryRay.colorAndDist.rgb;
			const float hitDistance = PrimaryRay.colorAndDist.w;
			const float objectId = PrimaryRay.normalAndObjId.w;
			const vec3 normal = PrimaryRay.normalAndObjId.xyz;
			if (hitDistance < 0.0f) {
				finalColor += hitColor;
				break;
			}
			else {
				const vec3 hitNormal = PrimaryRay.normalAndObjId.xyz;
				const vec3 hitPos = origin + direction * hitDistance;
				vec3 toLight = abs(normalize(hitPos - LightSource));

				if (objectId == OBJECT_ID_BOX1) {
					finalColor += hitColor * 0.1f;
					origin = hitPos + direction * 0.001f;
					direction = vec3(normal + RandomInUnitSphere(seed));					
				}
				else if (objectId == OBJECT_ID_BOX2) { //ROOM					
					origin = hitPos + direction * 0.001f;
					direction = vec3(hitNormal + RandomInUnitSphere(seed));
					traceNV(Scene, rayFlags, cullMask, 0, stbRecordStride, 0, origin, tmin, direction, tmax, 0);
				}
				else if (objectId == OBJECT_ID_BOX3) {
					const float dot = dot(direction, normal);
					const vec3 outwardDormal = dot > 0 ? -normal : normal;
					const float niOverNt = dot > 0 ? RefractionIndex : 1 / RefractionIndex;
					const float cosine = dot > 0 ? RefractionIndex * dot : -dot;
					const vec3 refracted = refract(direction, outwardDormal, niOverNt);
					const float reflectProb = refracted != vec3(0) ? Schlick(cosine, RefractionIndex) : 1;
					const vec3 scatter = RandomFloat(seed) < reflectProb ? reflect(direction, normal) : refracted;
					origin = hitPos + direction * 0.001f;
					direction = vec3(scatter);				
				}
				else if (objectId == OBJECT_ID_LIGHT_PLANE) {
					origin = hitPos + direction * 0.001f;
					finalColor += vec3(1);
					direction = vec3(normal + RandomInUnitSphere(seed));
					break;
				}
				else {
					origin = hitPos + direction * 0.001f;
					direction = vec3(hitNormal + RandomInUnitSphere(seed));
					break;
				}
			}
	
		}

		curPixel = vec2(gl_LaunchIDNV.x + RandomFloat(seed), gl_LaunchIDNV.y + RandomFloat(seed));
	}
	finalColor = finalColor / SWS_MAX_RAYS;
	finalColor = sqrt(finalColor); //gamma
	imageStore(ResultImage, ivec2(gl_LaunchIDNV.xy), vec4(LinearToSrgb(finalColor), 1.0f));
}
