//help: CupidC Feature 3 demo — class keyword with methods and implicit self
//help: Usage: feature3_class
//help: Verifies class fields, methods, self, and method-call sugar.

class Vec2 {
    I32 x;
    I32 y;

    U0 Set(I32 nx, I32 ny) {
        self->x = nx;
        self->y = ny;
    }

    I32 Dot(Vec2 *other) {
        return self->x * other->x + self->y * other->y;
    }
};

void main() {
    Vec2 v;
    v.Set(3, 4);

    I32 d = v.Dot(&v);

    print("dot=");
    print_int(d);
    print("\n");

    if (d == 25) {
        println("feature3_class: PASS");
    } else {
        println("feature3_class: FAIL");
    }
}
