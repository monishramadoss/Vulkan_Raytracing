#version 460
#extension GL_NV_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../shared_with_shaders.h"

layout(set = SWS_MATIDS_SET, binding = 0, std430) readonly buffer MatIDsBuffer {
    uint MatIDs[];
} MatIDsArray[];

layout(set = SWS_ATTRIBS_SET, binding = 0, std430) readonly buffer AttribsBuffer {
    VertexAttribute VertexAttribs[];
} AttribsArray[];

layout(set = SWS_FACES_SET, binding = 0, std430) readonly buffer FacesBuffer {
    uvec4 Faces[];
} FacesArray[];

layout(set = SWS_TEXTURES_SET, binding = 0) uniform sampler2D TexturesArray[];

layout(location = SWS_LOC_PRIMARY_RAY) rayPayloadInNV RayPayload PrimaryRay;
                                       hitAttributeNV vec2 HitAttribs;


void main() {
    const vec3 barycentrics = vec3(1.0f - HitAttribs.x - HitAttribs.y, HitAttribs.x, HitAttribs.y);

    const uint matID = MatIDsArray[nonuniformEXT(gl_InstanceCustomIndexNV)].MatIDs[gl_PrimitiveID];

    const uvec4 face = FacesArray[nonuniformEXT(gl_InstanceCustomIndexNV)].Faces[gl_PrimitiveID];

    VertexAttribute v0 = AttribsArray[nonuniformEXT(gl_InstanceCustomIndexNV)].VertexAttribs[int(face.x)];
    VertexAttribute v1 = AttribsArray[nonuniformEXT(gl_InstanceCustomIndexNV)].VertexAttribs[int(face.y)];
    VertexAttribute v2 = AttribsArray[nonuniformEXT(gl_InstanceCustomIndexNV)].VertexAttribs[int(face.z)];

    // interpolate our vertex attribs
    const vec3 normal = normalize(BaryLerp(v0.normal.xyz, v1.normal.xyz, v2.normal.xyz, barycentrics));
	vec3 texel;
   	
	
    const float objId = float(gl_InstanceCustomIndexNV);
	if (objId == 0) {
		texel = vec3(0.0f, 0.0f, 1.0f);
	}
	else if (objId == 1) { //global box
		texel = vec3(1.0f, 1.0f, 1.0f);
	}
	else if (objId == 2) {
		texel = vec3(0.0f, 1.0f, 0.0f);
	}
	else if (objId == 3) {
		texel = vec3(0.1f, 0.1f, 0.1f);
	}
	else {
		texel = vec3(0.0f, 0.0f, 0.0f);
	}
	

    PrimaryRay.colorAndDist = vec4(texel, gl_HitTNV);
    PrimaryRay.normalAndObjId = vec4(normal, objId);
}
