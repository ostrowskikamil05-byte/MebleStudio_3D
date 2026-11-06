#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=3) in uint owner;
layout(location=4) in uint flags;
layout(set=0,binding=0,std140) uniform Frame {
    mat4 viewProjection;
    vec4 lightDirection;
    vec4 options;
} frame;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec4 baseColor;
layout(location=3) flat out uint ownerId;
layout(location=4) flat out uint vertexFlags;
void main() {
    gl_Position=frame.viewProjection*vec4(position,1.0);
    gl_Position.y=-gl_Position.y;
    worldPosition=position; worldNormal=normal; baseColor=color;
    ownerId=owner; vertexFlags=flags;
}
