#include "Gizmos.hpp"

#include "AdjustmentModes.hpp"
#include "Config.hpp"
#include "Tracking.hpp"
#include "main.hpp"

#include "bsml/shared/Helpers/getters.hpp"

#include "UnityEngine/Collider.hpp"
#include "UnityEngine/Color.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Material.hpp"
#include "UnityEngine/MeshRenderer.hpp"
#include "UnityEngine/Object.hpp"
#include "UnityEngine/PrimitiveType.hpp"
#include "UnityEngine/Renderer.hpp"
#include "UnityEngine/Shader.hpp"
#include "UnityEngine/Transform.hpp"

using namespace UnityEngine;

namespace EasyOffset::Gizmos {

struct Shape {
    UnityW<GameObject> go;
    UnityW<Material> material;
};

struct HandGizmos {
    Shape pivot, saber, axisX, axisY, axisZ;
    bool orthonormalFocus = false;
    bool sphericalFocus = false;
    float zoom = 1;
};

static UnityW<GameObject> root;
static HandGizmos hands[2];
static Shape plane;
static UnityW<Material> baseMaterial;

static HandGizmos& G(Hand hand) { return hands[hand == Hand::Left ? 0 : 1]; }

static Material* GetBaseMaterial() {
    if (baseMaterial) return baseMaterial.ptr();
    // Unlit shader from the game; BSML's UI material is the fallback
    auto shader = Shader::Find("Custom/SimpleLit");
    if (shader) {
        baseMaterial = Material::New_ctor(shader);
    } else {
        auto uiMat = BSML::Helpers::GetUINoGlowMat();
        if (uiMat) baseMaterial = Material::New_ctor(uiMat);
    }
    if (baseMaterial) Object::DontDestroyOnLoad(baseMaterial.ptr());
    return baseMaterial.ptr();
}

static Shape MakeShape(PrimitiveType type, Color color) {
    Shape s;
    s.go = GameObject::CreatePrimitive(type);
    if (!s.go) return s;
    s.go->get_transform()->SetParent(root->get_transform(), false);
    if (auto collider = s.go->GetComponent<Collider*>()) Object::DestroyImmediate(collider);
    auto renderer = s.go->GetComponent<MeshRenderer*>();
    if (renderer) {
        if (auto mat = GetBaseMaterial()) {
            s.material = Material::New_ctor(mat);
            s.material->set_color(color);
            renderer->set_sharedMaterial(s.material.ptr());
        }
    }
    return s;
}

static void SetColor(Shape& s, Color color) {
    if (s.material) s.material->set_color(color);
}

static void SetActive(Shape& s, bool active) {
    if (s.go && s.go->get_activeSelf() != active) s.go->SetActive(active);
}

// Places a cylinder between two tracking-space points
static void PlaceLine(Shape& s, Vec3 from, Vec3 to, float thickness) {
    if (!s.go) return;
    Vec3 dir = to - from;
    float length = dir.Magnitude();
    auto t = s.go->get_transform();
    t->set_localPosition((from + to) * 0.5f);
    t->set_localRotation(FromToRotation(Vec3::Up(), dir));
    t->set_localScale(Vec3(thickness, length * 0.5f, thickness));
}

static void PlaceSphere(Shape& s, Vec3 position, float diameter) {
    if (!s.go) return;
    auto t = s.go->get_transform();
    t->set_localPosition(position);
    t->set_localScale(Vec3(diameter, diameter, diameter));
}

static bool EnsureCreated(Transform* origin) {
    if (root) {
        if (root->get_transform()->get_parent().unsafePtr() != origin) root->get_transform()->SetParent(origin, false);
        return true;
    }
    if (!origin) return false;
    root = GameObject::New_ctor("EasyOffsetGizmos");
    root->get_transform()->SetParent(origin, false);
    for (auto& h : hands) {
        h.pivot = MakeShape(PrimitiveType::Sphere, Color(1, 1, 1, 1));
        h.saber = MakeShape(PrimitiveType::Cylinder, Color(1, 1, 1, 1));
        h.axisX = MakeShape(PrimitiveType::Cylinder, Color(1, 0.2f, 0.2f, 1));
        h.axisY = MakeShape(PrimitiveType::Cylinder, Color(0.2f, 1, 0.2f, 1));
        h.axisZ = MakeShape(PrimitiveType::Cylinder, Color(0.3f, 0.5f, 1, 1));
    }
    plane = MakeShape(PrimitiveType::Cube, Color(0.3f, 1, 0.3f, 0.35f));
    return true;
}

static UnityW<Transform> origin;

static bool ShouldShow() {
    if (!Config::IsPanelVisible || !Config::GetShowGizmos() || !Config::GetEnabled()) return false;
    auto mode = Config::GetAdjustmentMode();
    return mode != AdjustmentMode::None && mode != AdjustmentMode::RoomOffset;
}

static void UpdateHand(Hand hand) {
    auto& g = G(hand);
    auto const& controller = Tracking::Get(hand);
    auto const& offsets = Config::Get(hand);
    Vec3 pivot = controller.LocalToWorldPosition(offsets.PivotPosition);
    Quat saber = controller.LocalToWorldRotation(offsets.Rotation);
    bool focused = g.orthonormalFocus || g.sphericalFocus;

    PlaceSphere(g.pivot, pivot, focused ? 0.03f : 0.02f);
    SetColor(g.pivot, focused ? Color(1, 0.85f, 0.2f, 1) : Color(1, 1, 1, 1));

    float length = 0.5f * g.zoom;
    PlaceLine(g.saber, pivot, pivot + saber * Vec3::Forward() * length, g.sphericalFocus ? 0.008f : 0.005f);
    SetColor(g.saber, hand == Hand::Left ? Color(1, 0.3f, 0.3f, 1) : Color(0.3f, 0.5f, 1, 1));

    bool showAxes = g.orthonormalFocus;
    SetActive(g.axisX, showAxes);
    SetActive(g.axisY, showAxes);
    SetActive(g.axisZ, showAxes);
    if (showAxes) {
        float a = 0.08f;
        PlaceLine(g.axisX, pivot, pivot + controller.Rotation() * Vec3::Right() * a, 0.004f);
        PlaceLine(g.axisY, pivot, pivot + controller.Rotation() * Vec3::Up() * a, 0.004f);
        PlaceLine(g.axisZ, pivot, pivot + controller.Rotation() * Vec3::Forward() * a, 0.004f);
    }
}

static void OnTick(float) {
    bool show = ShouldShow();
    if (!show) {
        if (root && root->get_activeSelf()) root->SetActive(false);
        return;
    }
    if (!EnsureCreated(origin ? origin.ptr() : nullptr)) return;
    if (!root->get_activeSelf()) root->SetActive(true);

    UpdateHand(Hand::Left);
    UpdateHand(Hand::Right);

    auto const& bench = AdjustmentModes::GetBenchmark();
    bool showPlane = Config::GetAdjustmentMode() == AdjustmentMode::SwingBenchmark && bench.HasResults;
    SetActive(plane, showPlane);
    if (showPlane && plane.go) {
        auto t = plane.go->get_transform();
        t->set_localPosition(bench.PlanePosition);
        t->set_localRotation(bench.PlaneRotation);
        t->set_localScale(Vec3(1.6f, 1.6f, 0.002f));
        SetColor(plane, bench.IsSwingGood ? Color(0.3f, 1, 0.3f, 0.3f) : Color(1, 0.5f, 0.2f, 0.3f));
    }
}

void SetOrigin(Transform* transform) { origin = transform; }

void Initialize() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    Tracking::TickEvent.Subscribe(OnTick);
}

void SetOrthonormalFocus(Hand hand, bool focus) { G(hand).orthonormalFocus = focus; }
void SetSphericalFocus(Hand hand, bool focus) { G(hand).sphericalFocus = focus; }
void Zoom(Hand hand, float zoom) { G(hand).zoom = zoom; }
}
