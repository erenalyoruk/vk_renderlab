struct VertexOutput {
  float4 position : SV_Position;
  [[vk::location(0)]]
  float3 color : COLOR0;
};

static const float2 kTrianglePositions[3] = {
  float2(0.0, -0.5),
  float2(0.5, 0.5),
  float2(-0.5, 0.5),
};

static const float3 kTriangleColors[3] = {
  float3(1.0, 0.1, 0.1),
  float3(0.1, 1.0, 0.1),
  float3(0.1, 0.3, 1.0),
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID) {
  VertexOutput output;
  output.position = float4(kTrianglePositions[vertexId], 0.0, 1.0);
  output.color = kTriangleColors[vertexId];
  return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
  return float4(input.color, 1.0);
}
