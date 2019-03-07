#define MUL(a, b) (b * a)
#define GIN   2.2
#define GOUT  2.2
#define Y   1.1
#define I   1.1
#define Q   1.1 

const mat3x3 RGBtoYIQ = mat3x3(0.299,     0.587,     0.114,
                      0.595716, -0.274453, -0.321263,
                      0.211456, -0.522591,  0.311135);

const mat3x3 YIQtoRGB = mat3x3(1,  0.95629572,  0.62102442,
                      1, -0.27212210, -0.64738060,
                      1, -1.10698902,  1.70461500);

const float3 YIQ_lo = float3(0, -0.595716, -0.522591);
const float3 YIQ_hi = float3(1,  0.595716,  0.522591);

float4 applyNatural(float3 c)
{
    c = pow(c, float3(GIN, GIN, GIN));
    c = MUL(RGBtoYIQ, c);
    c = float3(pow(c.x, Y), c.y * I, c.z * Q);
    c = clamp(c, YIQ_lo, YIQ_hi);
    c = MUL(YIQtoRGB, c);
    c = pow(c, float3(1.0/GOUT, 1.0/GOUT, 1.0/GOUT));
    return float4(c, 1.0);
}

void main()
{
	float4 c0 = Sample();
	float3 c1;
	c1.r = c0.r;
	c1.g = c0.g;
	c1.b = c0.b;
    SetOutput(applyNatural(c1));
}