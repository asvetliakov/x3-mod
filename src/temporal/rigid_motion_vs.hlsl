// Original rigid POSITION replay. Rows are the actual submitted shader constants,
// including raster jitter. Do not transpose, multiply matrices, or jitter again.
float4 current0 : register(c0);
float4 current1 : register(c1);
float4 current2 : register(c2);
float4 current3 : register(c3);
float4 previous0 : register(c4);
float4 previous1 : register(c5);
float4 previous2 : register(c6);
float4 previous3 : register(c7);
void main(float4 position : POSITION0, out float4 clip : POSITION0,
          out float4 previous : TEXCOORD0) {
    float4 p = float4(position.xyz, 1);
    clip = float4(dot(current0,p), dot(current1,p), dot(current2,p), dot(current3,p));
    previous = float4(dot(previous0,p), dot(previous1,p), dot(previous2,p), dot(previous3,p));
}
