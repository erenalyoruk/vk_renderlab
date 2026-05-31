struct SceneUniforms {
  float4x4 mvp;
};

[[vk::binding(0, 0)]]
ConstantBuffer<SceneUniforms> gScene;

struct VertexInput {
  [[vk::location(0)]]
  float3 position : POSITION0;
  [[vk::location(1)]]
  float3 color : COLOR0;
};

struct VertexOutput {
  float4 position : SV_Position;
  [[vk::location(0)]]
  float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
  VertexOutput output;
  output.position = mul(gScene.mvp, float4(input.position, 1.0));
  output.color = input.color;
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  return float4(input.color, 1.0);
}
