#version 450
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec4 baseColor;
layout(location=3) flat in uint ownerId;
layout(location=4) flat in uint vertexFlags;
layout(set=0,binding=0,std140) uniform Frame {
    mat4 viewProjection; vec4 lightDirection; vec4 options;
} frame;
layout(push_constant) uniform Pass { int ghost; } pass;
layout(location=0) out vec4 outputColor;
void main() {
    vec3 normal=worldNormal*inversesqrt(max(dot(worldNormal,worldNormal),.00001));
    if ((vertexFlags & 8u)!=0u) {
        vec3 cameraNear=-normalize(vec3(frame.viewProjection[0][2],
                                       frame.viewProjection[1][2],
                                       frame.viewProjection[2][2]));
        float facing=dot(worldNormal,cameraNear);
        if ((abs(worldNormal.z)>.5 && facing<=.001) ||
            (abs(worldNormal.z)<=.5 && facing>=-.001)) discard;
        // Room walls have outward normals. Shade the retained inward-facing
        // wall surface after applying the same camera cutaway in both APIs.
        if (abs(worldNormal.z)<=.5 && facing<0) normal=-normal;
    }
    bool xray=frame.options.y>0.5;
    bool board=(vertexFlags & (4u|8u|16u))==0u;
    bool transparent=xray && (frame.options.y>1.5 ? (vertexFlags & 32u)!=0u : board);
    if ((pass.ghost==0 && transparent) || (pass.ghost==1 && !transparent)) discard;
    vec3 light=frame.lightDirection.xyz*inversesqrt(max(dot(frame.lightDirection.xyz,frame.lightDirection.xyz),.00001));
    float diffuse=clamp(dot(normal,light),0.0,1.0);
    float secondary=clamp(dot(normal,normalize(vec3(.65,.25,.7))),0.0,1.0);
    vec3 c=clamp(baseColor.rgb,0.0,1.0);
    if ((vertexFlags & 2u)!=0u) {
        float wave=sin(worldPosition.z*.031+sin(worldPosition.x*.009)*1.7+
                       sin(worldPosition.y*.011)*1.1);
        float fine=sin(worldPosition.z*.31+sin(worldPosition.x*.017)*2.0);
        c*=.96+.045*wave+.012*fine;
    }
    c*=((vertexFlags & 1u)!=0u) ? 1.35 : (.40+.52*diffuse+.16*secondary);
    c*=clamp(frame.options.x,.15,3.0);
    bool selected=(vertexFlags & 64u)!=0u || (frame.options.z>=0 && abs(float(ownerId)-frame.options.z)<.5);
    if (selected) c=mix(c,vec3(1.0,.62,.13),.28);
    outputColor=vec4(clamp(c,0.0,1.0),transparent ? (selected ? .36 : .20) : 1.0);
}
