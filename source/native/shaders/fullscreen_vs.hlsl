// Полноэкранный прямоугольник для шейдеров мода: позиция уже в координатах отсечения.
struct VSOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VSOut main(float4 pos : POSITION, float2 uv : TEXCOORD0) {
    VSOut o;
    o.pos = pos;
    o.uv = uv;
    return o;
}
