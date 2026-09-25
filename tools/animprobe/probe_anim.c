/* probe_anim: Windows.UI.Composition animations as objects -- what key frame and expression animations,
 * easing functions, property sets and batches start out as, and what StartAnimation refuses. */
#define COBJMACROS
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "windef.h"
#include "initguid.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winstring.h"
#include "roapi.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Foundation_Numerics
#define WIDL_using_Windows_Graphics
#define WIDL_using_Windows_Graphics_DirectX
#define WIDL_using_Windows_Graphics_Effects
#define WIDL_using_Windows_System
#define WIDL_using_Windows_UI
#define WIDL_using_Windows_UI_Core
#define WIDL_using_Windows_UI_Composition
#define WIDL_using_Windows_UI_Composition_Core
#define WIDL_using_Windows_UI_Composition_Desktop
#include "windows.foundation.h"
#include "windows.ui.composition.h"
#include "windows.ui.composition.interop.h"

static HSTRING S(const WCHAR *s)
{
    HSTRING h = NULL;
    WindowsCreateString(s, wcslen(s), &h);
    return h;
}

static void print_class(const char *what, void *obj)
{
    IInspectable *inspectable;
    HSTRING name = NULL;
    HRESULT hr;
    if (!obj) { printf("%s: (null)\n", what); return; }
    IUnknown_QueryInterface((IUnknown *)obj, &IID_IInspectable, (void **)&inspectable);
    hr = IInspectable_GetRuntimeClassName(inspectable, &name);
    printf("%s: class %#lx %ls\n", what, hr, name ? WindowsGetStringRawBuffer(name, NULL) : L"(null)");
    WindowsDeleteString(name);
    IInspectable_Release(inspectable);
}

static void qi(const char *what, void *obj, const IID *iid, const char *name)
{
    IUnknown *unk = NULL;
    HRESULT hr = IUnknown_QueryInterface((IUnknown *)obj, iid, (void **)&unk);
    printf("  %s QI %s %#lx\n", what, name, hr);
    if (unk) IUnknown_Release(unk);
}
#define QI(what, obj, i) qi(what, obj, &IID_##i, #i)

typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;

static HRESULT start(void *object, const WCHAR *property, void *animation)
{
    ICompositionObject *obj;
    ICompositionAnimation *anim = NULL;
    HSTRING str = S(property);
    HRESULT hr;
    IUnknown_QueryInterface((IUnknown *)object, &IID_ICompositionObject, (void **)&obj);
    if (animation) IUnknown_QueryInterface((IUnknown *)animation, &IID_ICompositionAnimation, (void **)&anim);
    hr = ICompositionObject_StartAnimation(obj, str, anim);
    WindowsDeleteString(str);
    if (anim) ICompositionAnimation_Release(anim);
    ICompositionObject_Release(obj);
    return hr;
}

static HRESULT stop(void *object, const WCHAR *property)
{
    ICompositionObject *obj;
    HSTRING str = S(property);
    HRESULT hr;
    IUnknown_QueryInterface((IUnknown *)object, &IID_ICompositionObject, (void **)&obj);
    hr = ICompositionObject_StopAnimation(obj, str);
    WindowsDeleteString(str);
    ICompositionObject_Release(obj);
    return hr;
}

static ICompositor *the_compositor;

static IScalarKeyFrameAnimation *scalar_frame_animation(ICompositor *compositor)
{
    IScalarKeyFrameAnimation *scalar = NULL;
    ICompositor_CreateScalarKeyFrameAnimation(compositor, &scalar);
    IScalarKeyFrameAnimation_InsertKeyFrame(scalar, 1.0f, 5.0f);
    return scalar;
}

/* a scalar animation of one key frame, and optionally an expression key frame too */
static HRESULT scalar_start(void *object, const WCHAR *property, const WCHAR *expression_frame)
{
    IScalarKeyFrameAnimation *scalar = NULL;
    IKeyFrameAnimation *keyframe;
    HRESULT hr;

    ICompositor_CreateScalarKeyFrameAnimation(the_compositor, &scalar);
    IScalarKeyFrameAnimation_InsertKeyFrame(scalar, 1.0f, 0.5f);
    if (expression_frame)
    {
        HSTRING str = S(expression_frame);
        IScalarKeyFrameAnimation_QueryInterface(scalar, &IID_IKeyFrameAnimation, (void **)&keyframe);
        IKeyFrameAnimation_InsertExpressionKeyFrame(keyframe, 0.5f, str);
        IKeyFrameAnimation_Release(keyframe);
        WindowsDeleteString(str);
    }
    hr = start(object, property, scalar);
    IScalarKeyFrameAnimation_Release(scalar);
    return hr;
}

static void expression_start(ICompositor *compositor, void *object, const WCHAR *property, const WCHAR *text)
{
    IExpressionAnimation *expression = NULL;
    HSTRING str = S(text);
    HRESULT hr = ICompositor_CreateExpressionAnimationWithExpression(compositor, str, &expression);
    WindowsDeleteString(str);
    printf("  expression %ls: create %#lx", text, hr);
    if (expression)
    {
        hr = start(object, property, expression);
        printf(", start on %ls %#lx", property, hr);
        IExpressionAnimation_Release(expression);
    }
    printf("\n");
}

int main(void)
{
    ICompositorController *controller = NULL;
    ICompositor *compositor = NULL;
    ICompositor2 *compositor2;
    ICompositor6 *compositor6;
    ISpriteVisual *sprite = NULL;
    IVisual *visual;
    IInspectable *inspectable;
    IScalarKeyFrameAnimation *scalar = NULL;
    IVector3KeyFrameAnimation *vector3 = NULL;
    IKeyFrameAnimation *keyframe;
    IKeyFrameAnimation2 *keyframe2;
    IKeyFrameAnimation3 *keyframe3;
    ICompositionAnimation *animation;
    ICompositionAnimation2 *animation2;
    IExpressionAnimation *expression = NULL;
    ICompositionPropertySet *props = NULL;
    ICompositionPropertySet2 *props2;
    ILinearEasingFunction *linear = NULL;
    ICubicBezierEasingFunction *bezier = NULL;
    IStepEasingFunction *step = NULL;
    ICompositionScopedBatch *batch = NULL;
    ICompositionCommitBatch *commit = NULL;
    ICompositionEasingFunction *easing;
    CompositionGetValueStatus status;
    AnimationIterationBehavior iteration;
    AnimationStopBehavior stop_behavior;
    AnimationDirection direction;
    AnimationDelayBehavior delay_behavior;
    TimeSpan span;
    Vector2 v2;
    Vector3 v3;
    FLOAT f;
    INT32 i;
    boolean b;
    HSTRING str;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_SINGLETHREADED);
    {
        HRESULT (WINAPI *create)(DispatcherQueueOptions_, IUnknown **) =
            (void *)GetProcAddress(LoadLibraryW(L"coremessaging.dll"), "CreateDispatcherQueueController");
        DispatcherQueueOptions_ options = {sizeof(options), 2, 2};
        IUnknown *queue = NULL;
        hr = create(options, &queue);
        printf("CreateDispatcherQueueController %#lx\n", hr);
    }
    str = S(L"Windows.UI.Composition.Core.CompositorController");
    hr = RoActivateInstance(str, &inspectable);
    WindowsDeleteString(str);
    printf("activate CompositorController %#lx\n", hr);
    if (FAILED(hr)) return 1;
    IInspectable_QueryInterface(inspectable, &IID_ICompositorController, (void **)&controller);
    IInspectable_Release(inspectable);
    ICompositorController_get_Compositor(controller, &compositor);
    the_compositor = compositor;
    ICompositor_QueryInterface(compositor, &IID_ICompositor2, (void **)&compositor2);
    ICompositor_CreateSpriteVisual(compositor, &sprite);
    ISpriteVisual_QueryInterface(sprite, &IID_IVisual, (void **)&visual);

    /* key frame animations */
    hr = ICompositor_CreateScalarKeyFrameAnimation(compositor, &scalar);
    printf("CreateScalarKeyFrameAnimation %#lx\n", hr);
    print_class("scalar", scalar);
    QI("scalar", scalar, IKeyFrameAnimation);
    QI("scalar", scalar, IKeyFrameAnimation2);
    QI("scalar", scalar, IKeyFrameAnimation3);
    QI("scalar", scalar, ICompositionAnimation);
    QI("scalar", scalar, ICompositionAnimation2);
    QI("scalar", scalar, ICompositionAnimation3);
    QI("scalar", scalar, ICompositionAnimation4);
    QI("scalar", scalar, ICompositionAnimationBase);
    QI("scalar", scalar, IVector3KeyFrameAnimation);
    IScalarKeyFrameAnimation_QueryInterface(scalar, &IID_IKeyFrameAnimation, (void **)&keyframe);
    span.Duration = -1; IKeyFrameAnimation_get_Duration(keyframe, &span); printf("  Duration %I64d\n", span.Duration);
    span.Duration = -1; IKeyFrameAnimation_get_DelayTime(keyframe, &span); printf("  DelayTime %I64d\n", span.Duration);
    iteration = -1; IKeyFrameAnimation_get_IterationBehavior(keyframe, &iteration); printf("  IterationBehavior %d\n", iteration);
    i = -1; IKeyFrameAnimation_get_IterationCount(keyframe, &i); printf("  IterationCount %d\n", i);
    i = -1; IKeyFrameAnimation_get_KeyFrameCount(keyframe, &i); printf("  KeyFrameCount %d\n", i);
    stop_behavior = -1; IKeyFrameAnimation_get_StopBehavior(keyframe, &stop_behavior); printf("  StopBehavior %d\n", stop_behavior);
    IKeyFrameAnimation_QueryInterface(keyframe, &IID_IKeyFrameAnimation2, (void **)&keyframe2);
    direction = -1; IKeyFrameAnimation2_get_Direction(keyframe2, &direction); printf("  Direction %d\n", direction);
    IKeyFrameAnimation_QueryInterface(keyframe, &IID_IKeyFrameAnimation3, (void **)&keyframe3);
    delay_behavior = -1; IKeyFrameAnimation3_get_DelayBehavior(keyframe3, &delay_behavior); printf("  DelayBehavior %d\n", delay_behavior);
    IScalarKeyFrameAnimation_QueryInterface(scalar, &IID_ICompositionAnimation2, (void **)&animation2);
    str = (HSTRING)0xdeadbeef;
    hr = ICompositionAnimation2_get_Target(animation2, &str);
    printf("  Target %#lx %p\n", hr, str);
    hr = start(sprite, L"Opacity", scalar);
    printf("  StartAnimation with no key frames %#lx\n", hr);
    hr = IScalarKeyFrameAnimation_InsertKeyFrame(scalar, 1.5f, 1.0f);
    printf("  InsertKeyFrame(1.5) %#lx\n", hr);
    hr = IScalarKeyFrameAnimation_InsertKeyFrame(scalar, -0.5f, 1.0f);
    printf("  InsertKeyFrame(-0.5) %#lx\n", hr);
    hr = IScalarKeyFrameAnimation_InsertKeyFrame(scalar, 1.0f, 0.5f);
    printf("  InsertKeyFrame(1, 0.5) %#lx\n", hr);
    hr = IScalarKeyFrameAnimation_InsertKeyFrame(scalar, 1.0f, 0.25f);
    printf("  InsertKeyFrame(1, 0.25) again %#lx\n", hr);
    i = -1; IKeyFrameAnimation_get_KeyFrameCount(keyframe, &i); printf("  KeyFrameCount %d\n", i);
    str = S(L"this.StartingValue * 2");
    hr = IKeyFrameAnimation_InsertExpressionKeyFrame(keyframe, 0.5f, str);
    WindowsDeleteString(str);
    printf("  InsertExpressionKeyFrame %#lx\n", hr);
    str = S(L"this.StartingValue +* 2");
    hr = IKeyFrameAnimation_InsertExpressionKeyFrame(keyframe, 0.25f, str);
    WindowsDeleteString(str);
    printf("  InsertExpressionKeyFrame(bad) %#lx\n", hr);
    i = -1; IKeyFrameAnimation_get_KeyFrameCount(keyframe, &i); printf("  KeyFrameCount %d\n", i);
    span.Duration = 0;
    hr = IKeyFrameAnimation_put_Duration(keyframe, span);
    printf("  put_Duration(0) %#lx\n", hr);
    span.Duration = 5000;
    hr = IKeyFrameAnimation_put_Duration(keyframe, span);
    printf("  put_Duration(0.5ms) %#lx\n", hr);
    span.Duration = -10000;
    hr = IKeyFrameAnimation_put_Duration(keyframe, span);
    printf("  put_Duration(-1ms) %#lx\n", hr);
    span.Duration = -1; IKeyFrameAnimation_get_Duration(keyframe, &span); printf("  Duration %I64d\n", span.Duration);
    span.Duration = -10000;
    hr = IKeyFrameAnimation_put_DelayTime(keyframe, span);
    printf("  put_DelayTime(-1ms) %#lx\n", hr);
    hr = IKeyFrameAnimation_put_IterationCount(keyframe, 0);
    printf("  put_IterationCount(0) %#lx\n", hr);
    hr = IKeyFrameAnimation_put_IterationCount(keyframe, -1);
    printf("  put_IterationCount(-1) %#lx\n", hr);
    hr = start(sprite, L"Opacity", scalar);
    printf("  StartAnimation(Opacity) with a bad expression key frame %#lx\n", hr);
    hr = start(sprite, L"Opacity", scalar);
    printf("  StartAnimation(Opacity) with it again %#lx\n", hr);
    printf("fresh scalar animations:\n");
    printf("  Opacity %#lx\n", scalar_start(sprite, L"Opacity", NULL));
    printf("  Opacity again %#lx\n", scalar_start(sprite, L"Opacity", NULL));
    printf("  opacity %#lx\n", scalar_start(sprite, L"opacity", NULL));
    printf("  Nonexistent %#lx\n", scalar_start(sprite, L"Nonexistent", NULL));
    printf("  Offset %#lx\n", scalar_start(sprite, L"Offset", NULL));
    printf("  Offset.X %#lx\n", scalar_start(sprite, L"Offset.X", NULL));
    printf("  offset.x %#lx\n", scalar_start(sprite, L"offset.x", NULL));
    printf("  Offset.Q %#lx\n", scalar_start(sprite, L"Offset.Q", NULL));
    printf("  Offset.XY %#lx\n", scalar_start(sprite, L"Offset.XY", NULL));
    printf("  Size.X %#lx\n", scalar_start(sprite, L"Size.X", NULL));
    printf("  RotationAngle %#lx\n", scalar_start(sprite, L"RotationAngle", NULL));
    printf("  RotationAngleInDegrees %#lx\n", scalar_start(sprite, L"RotationAngleInDegrees", NULL));
    printf("  IsVisible %#lx\n", scalar_start(sprite, L"IsVisible", NULL));
    printf("  Opacity with a good expression frame %#lx\n", scalar_start(sprite, L"Opacity", L"this.StartingValue * 2"));
    printf("  Opacity with a bad expression frame %#lx\n", scalar_start(sprite, L"Opacity", L"this.StartingValue +* 2"));
    printf("  Opacity with a frame of a missing name %#lx\n", scalar_start(sprite, L"Opacity", L"missing.Value"));
    printf("  Opacity with a frame of a vector %#lx\n", scalar_start(sprite, L"Opacity", L"Vector2(1, 2)"));
    printf("  Opacity with a frame of a missing function %#lx\n", scalar_start(sprite, L"Opacity", L"Foo(1)"));
    hr = stop(sprite, L"Opacity");
    printf("  StopAnimation(Opacity) %#lx\n", hr);
    hr = stop(sprite, L"Opacity");
    printf("  StopAnimation(Opacity) again %#lx\n", hr);
    hr = stop(sprite, L"Nonexistent");
    printf("  StopAnimation(Nonexistent) %#lx\n", hr);
    IVisual_get_Opacity(visual, &f);
    printf("  Opacity %g\n", f);

    hr = ICompositor_CreateVector3KeyFrameAnimation(compositor, &vector3);
    printf("CreateVector3KeyFrameAnimation %#lx\n", hr);
    print_class("vector3", vector3);
    IVector3KeyFrameAnimation_InsertKeyFrame(vector3, 1.0f, (Vector3){1, 2, 3});
    hr = start(sprite, L"Offset", vector3);
    printf("  StartAnimation(Offset) %#lx\n", hr);
    hr = start(sprite, L"Size", vector3);
    printf("  StartAnimation(Size) %#lx\n", hr);
    hr = start(sprite, L"Offset.XY", vector3);
    printf("  StartAnimation(Offset.XY) %#lx\n", hr);
    hr = start(sprite, L"Scale", vector3);
    printf("  StartAnimation(Scale) %#lx\n", hr);
    IVisual_get_Offset(visual, &v3);
    printf("  Offset %g %g %g\n", v3.X, v3.Y, v3.Z);

    if (SUCCEEDED(ICompositor_QueryInterface(compositor, &IID_ICompositor6, (void **)&compositor6)))
    {
        IBooleanKeyFrameAnimation *boolean_anim = NULL;
        hr = ICompositor6_CreateBooleanKeyFrameAnimation(compositor6, &boolean_anim);
        printf("CreateBooleanKeyFrameAnimation %#lx\n", hr);
        print_class("boolean", boolean_anim);
        if (boolean_anim)
        {
            IBooleanKeyFrameAnimation_InsertKeyFrame(boolean_anim, 1.0f, FALSE);
            hr = start(sprite, L"IsVisible", boolean_anim);
            printf("  StartAnimation(IsVisible) %#lx\n", hr);
            IBooleanKeyFrameAnimation_Release(boolean_anim);
        }
        ICompositor6_Release(compositor6);
    }

    /* expression animations */
    hr = ICompositor_CreateExpressionAnimation(compositor, &expression);
    printf("CreateExpressionAnimation %#lx\n", hr);
    print_class("expression", expression);
    QI("expression", expression, IKeyFrameAnimation);
    str = (HSTRING)0xdeadbeef;
    hr = IExpressionAnimation_get_Expression(expression, &str);
    printf("  Expression %#lx %p\n", hr, str);
    hr = start(sprite, L"Opacity", expression);
    printf("  StartAnimation with no expression %#lx\n", hr);
    str = S(L"1 +");
    hr = IExpressionAnimation_put_Expression(expression, str);
    WindowsDeleteString(str);
    printf("  put_Expression(bad) %#lx\n", hr);
    hr = start(sprite, L"Opacity", expression);
    printf("  StartAnimation(bad) %#lx\n", hr);
    expression_start(compositor, sprite, L"Opacity", L"0.5");
    expression_start(compositor, sprite, L"Opacity", L"Vector2(1, 2)");
    expression_start(compositor, sprite, L"Offset", L"0.5");
    expression_start(compositor, sprite, L"Offset", L"Vector3(1, 2, 3)");
    expression_start(compositor, sprite, L"Offset", L"Vector2(1, 2)");
    expression_start(compositor, sprite, L"Offset.X", L"7");
    expression_start(compositor, sprite, L"Opacity", L"missing.Opacity");
    expression_start(compositor, sprite, L"Opacity", L"Foo(1)");
    expression_start(compositor, sprite, L"Opacity", L"this.Target.Size.X / 100");
    expression_start(compositor, sprite, L"Opacity", L"this.StartingValue");
    expression_start(compositor, sprite, L"IsVisible", L"true");
    expression_start(compositor, sprite, L"IsVisible", L"1");
    expression_start(compositor, sprite, L"Size", L"Vector2(1, 2) * 3");
    expression_start(compositor, sprite, L"TransformMatrix", L"Matrix4x4.CreateFromTranslation(Vector3(1, 2, 3))");
    expression_start(compositor, sprite, L"RotationAngleInDegrees", L"45");
    expression_start(compositor, sprite, L"Orientation", L"Quaternion(0, 0, 0, 1)");
    IExpressionAnimation_QueryInterface(expression, &IID_ICompositionAnimation, (void **)&animation);
    str = S(L"visual");
    hr = ICompositionAnimation_SetReferenceParameter(animation, str, NULL);
    printf("  SetReferenceParameter(NULL) %#lx\n", hr);
    hr = ICompositionAnimation_SetScalarParameter(animation, str, 1.0f);
    printf("  SetScalarParameter same key %#lx\n", hr);
    hr = ICompositionAnimation_ClearParameter(animation, str);
    printf("  ClearParameter %#lx\n", hr);
    hr = ICompositionAnimation_ClearParameter(animation, str);
    printf("  ClearParameter again %#lx\n", hr);
    WindowsDeleteString(str);
    hr = ICompositionAnimation_SetScalarParameter(animation, NULL, 1.0f);
    printf("  SetScalarParameter(NULL key) %#lx\n", hr);

    /* property sets */
    hr = ICompositor_CreatePropertySet(compositor, &props);
    printf("CreatePropertySet %#lx\n", hr);
    print_class("props", props);
    QI("props", props, ICompositionPropertySet2);
    QI("props", props, ICompositionObject);
    str = S(L"foo");
    f = -1; status = -1;
    hr = ICompositionPropertySet_TryGetScalar(props, str, &f, &status);
    printf("  TryGetScalar(missing) %#lx status %d value %g\n", hr, status, f);
    hr = ICompositionPropertySet_InsertScalar(props, str, 2.5f);
    printf("  InsertScalar %#lx\n", hr);
    f = -1; status = -1;
    hr = ICompositionPropertySet_TryGetScalar(props, str, &f, &status);
    printf("  TryGetScalar %#lx status %d value %g\n", hr, status, f);
    v2.X = v2.Y = -1; status = -1;
    hr = ICompositionPropertySet_TryGetVector2(props, str, &v2, &status);
    printf("  TryGetVector2(scalar) %#lx status %d value %g %g\n", hr, status, v2.X, v2.Y);
    hr = ICompositionPropertySet_InsertVector2(props, str, (Vector2){3, 4});
    printf("  InsertVector2 over a scalar %#lx\n", hr);
    status = -1;
    hr = ICompositionPropertySet_TryGetVector2(props, str, &v2, &status);
    printf("  TryGetVector2 %#lx status %d value %g %g\n", hr, status, v2.X, v2.Y);
    WindowsDeleteString(str);
    str = S(L"FOO");
    status = -1;
    hr = ICompositionPropertySet_TryGetVector2(props, str, &v2, &status);
    printf("  TryGetVector2(FOO) %#lx status %d\n", hr, status);
    WindowsDeleteString(str);
    hr = ICompositionPropertySet_InsertScalar(props, NULL, 1.0f);
    printf("  InsertScalar(NULL) %#lx\n", hr);
    str = S(L"bar.baz");
    hr = ICompositionPropertySet_InsertScalar(props, str, 1.0f);
    printf("  InsertScalar(bar.baz) %#lx\n", hr);
    WindowsDeleteString(str);
    if (SUCCEEDED(ICompositionPropertySet_QueryInterface(props, &IID_ICompositionPropertySet2, (void **)&props2)))
    {
        str = S(L"flag");
        hr = ICompositionPropertySet2_InsertBoolean(props2, str, TRUE);
        b = 2; status = -1;
        ICompositionPropertySet2_TryGetBoolean(props2, str, &b, &status);
        printf("  InsertBoolean %#lx, TryGetBoolean status %d value %d\n", hr, status, b);
        WindowsDeleteString(str);
        ICompositionPropertySet2_Release(props2);
    }
    str = S(L"foo");
    expression_start(compositor, props, L"foo", L"Vector2(1, 1)");
    expression_start(compositor, props, L"foo.X", L"1");
    expression_start(compositor, props, L"nope", L"1");
    {
        ICompositionObject *object;
        ICompositionPropertySet *visual_props = NULL, *again = NULL;
        ISpriteVisual_QueryInterface(sprite, &IID_ICompositionObject, (void **)&object);
        hr = ICompositionObject_get_Properties(object, &visual_props);
        printf("visual Properties %#lx\n", hr);
        print_class("  visual props", visual_props);
        ICompositionObject_get_Properties(object, &again);
        printf("  same %d\n", visual_props == again);
        if (visual_props)
        {
            ICompositionPropertySet_InsertScalar(visual_props, str, 3.0f);
            expression_start(compositor, sprite, L"foo", L"1");
            ICompositionPropertySet_Release(visual_props);
        }
        if (again) ICompositionPropertySet_Release(again);
        ICompositionObject_Release(object);
    }
    WindowsDeleteString(str);

    /* easing functions */
    hr = ICompositor_CreateLinearEasingFunction(compositor, &linear);
    printf("CreateLinearEasingFunction %#lx\n", hr);
    print_class("linear", linear);
    QI("linear", linear, ICompositionEasingFunction);
    hr = ICompositor_CreateCubicBezierEasingFunction(compositor, (Vector2){0.1f, 0.2f}, (Vector2){0.3f, 0.4f}, &bezier);
    printf("CreateCubicBezierEasingFunction %#lx\n", hr);
    print_class("bezier", bezier);
    if (bezier)
    {
        ICubicBezierEasingFunction_get_ControlPoint1(bezier, &v2); printf("  ControlPoint1 %g %g\n", v2.X, v2.Y);
        ICubicBezierEasingFunction_get_ControlPoint2(bezier, &v2); printf("  ControlPoint2 %g %g\n", v2.X, v2.Y);
    }
    hr = ICompositor_CreateCubicBezierEasingFunction(compositor, (Vector2){1.5f, 0.2f}, (Vector2){0.3f, 0.4f}, &bezier);
    printf("CreateCubicBezierEasingFunction(x 1.5) %#lx\n", hr);
    hr = ICompositor_CreateCubicBezierEasingFunction(compositor, (Vector2){0.5f, 2.0f}, (Vector2){0.3f, -1.0f}, &bezier);
    printf("CreateCubicBezierEasingFunction(y 2, -1) %#lx\n", hr);
    hr = ICompositor2_CreateStepEasingFunction(compositor2, &step);
    printf("CreateStepEasingFunction %#lx\n", hr);
    print_class("step", step);
    if (step)
    {
        i = -1; IStepEasingFunction_get_StepCount(step, &i); printf("  StepCount %d\n", i);
        i = -1; IStepEasingFunction_get_InitialStep(step, &i); printf("  InitialStep %d\n", i);
        i = -1; IStepEasingFunction_get_FinalStep(step, &i); printf("  FinalStep %d\n", i);
        b = 2; IStepEasingFunction_get_IsInitialStepSingleFrame(step, &b); printf("  IsInitialStepSingleFrame %d\n", b);
        b = 2; IStepEasingFunction_get_IsFinalStepSingleFrame(step, &b); printf("  IsFinalStepSingleFrame %d\n", b);
        hr = IStepEasingFunction_put_StepCount(step, 0); printf("  put_StepCount(0) %#lx\n", hr);
        hr = IStepEasingFunction_put_StepCount(step, 4); printf("  put_StepCount(4) %#lx\n", hr);
        i = -1; IStepEasingFunction_get_FinalStep(step, &i); printf("  FinalStep %d\n", i);
    }
    step = NULL;
    hr = ICompositor2_CreateStepEasingFunctionWithStepCount(compositor2, 5, &step);
    printf("CreateStepEasingFunctionWithStepCount(5) %#lx\n", hr);
    if (step)
    {
        i = -1; IStepEasingFunction_get_StepCount(step, &i); printf("  StepCount %d\n", i);
        i = -1; IStepEasingFunction_get_FinalStep(step, &i); printf("  FinalStep %d\n", i);
    }
    IScalarKeyFrameAnimation_QueryInterface(scalar, &IID_ICompositionAnimation, (void **)&animation);
    if (linear)
    {
        ILinearEasingFunction_QueryInterface(linear, &IID_ICompositionEasingFunction, (void **)&easing);
        hr = IScalarKeyFrameAnimation_InsertKeyFrameWithEasingFunction(scalar, 0.75f, 1.0f, easing);
        printf("  InsertKeyFrameWithEasingFunction %#lx\n", hr);
        hr = IScalarKeyFrameAnimation_InsertKeyFrameWithEasingFunction(scalar, 0.75f, 1.0f, NULL);
        printf("  InsertKeyFrameWithEasingFunction(NULL) %#lx\n", hr);
        ICompositionEasingFunction_Release(easing);
    }

    /* batches */
    hr = ICompositor_CreateScopedBatch(compositor, CompositionBatchTypes_Animation, &batch);
    printf("CreateScopedBatch %#lx\n", hr);
    print_class("batch", batch);
    if (batch)
    {
        b = 2; ICompositionScopedBatch_get_IsActive(batch, &b); printf("  IsActive %d\n", b);
        b = 2; ICompositionScopedBatch_get_IsEnded(batch, &b); printf("  IsEnded %d\n", b);
        hr = ICompositionScopedBatch_Suspend(batch); printf("  Suspend %#lx\n", hr);
        b = 2; ICompositionScopedBatch_get_IsActive(batch, &b); printf("  IsActive %d\n", b);
        hr = ICompositionScopedBatch_Suspend(batch); printf("  Suspend again %#lx\n", hr);
        hr = ICompositionScopedBatch_Resume(batch); printf("  Resume %#lx\n", hr);
        hr = ICompositionScopedBatch_End(batch); printf("  End %#lx\n", hr);
        b = 2; ICompositionScopedBatch_get_IsActive(batch, &b); printf("  IsActive %d\n", b);
        b = 2; ICompositionScopedBatch_get_IsEnded(batch, &b); printf("  IsEnded %d\n", b);
        hr = ICompositionScopedBatch_End(batch); printf("  End again %#lx\n", hr);
        hr = ICompositionScopedBatch_Resume(batch); printf("  Resume after End %#lx\n", hr);
    }
    hr = ICompositor_CreateScopedBatch(compositor, 0, &batch);
    printf("CreateScopedBatch(0) %#lx\n", hr);
    hr = ICompositor_GetCommitBatch(compositor, CompositionBatchTypes_Animation, &commit);
    printf("GetCommitBatch %#lx\n", hr);
    print_class("commit", commit);
    if (commit)
    {
        ICompositionCommitBatch *again = NULL;
        b = 2; ICompositionCommitBatch_get_IsActive(commit, &b); printf("  IsActive %d\n", b);
        b = 2; ICompositionCommitBatch_get_IsEnded(commit, &b); printf("  IsEnded %d\n", b);
        ICompositor_GetCommitBatch(compositor, CompositionBatchTypes_Animation, &again);
        printf("  same %d\n", again == commit);
        ICompositorController_Commit(controller);
        b = 2; ICompositionCommitBatch_get_IsActive(commit, &b); printf("  after Commit IsActive %d\n", b);
        b = 2; ICompositionCommitBatch_get_IsEnded(commit, &b); printf("  after Commit IsEnded %d\n", b);
        if (again) ICompositionCommitBatch_Release(again);
        again = NULL;
        ICompositor_GetCommitBatch(compositor, CompositionBatchTypes_Animation, &again);
        printf("  same after Commit %d\n", again == commit);
    }

    /* drop shadows */
    {
        IDropShadow *shadow = NULL;
        IDropShadow2 *shadow2;
        ICompositionShadow *cshadow;
        ISpriteVisual2 *sprite2;
        ICompositionBrush *mask = (void *)0xdeadbeef;
        CompositionDropShadowSourcePolicy policy = -1;
        Color c = {0};

        hr = ICompositor2_CreateDropShadow(compositor2, &shadow);
        printf("CreateDropShadow %#lx\n", hr);
        print_class("shadow", shadow);
        if (shadow)
        {
            QI("shadow", shadow, ICompositionShadow);
            QI("shadow", shadow, IDropShadow2);
            QI("shadow", shadow, ICompositionObject);
            f = -1; IDropShadow_get_BlurRadius(shadow, &f); printf("  BlurRadius %g\n", f);
            IDropShadow_get_Color(shadow, &c); printf("  Color %02x%02x%02x%02x\n", c.A, c.R, c.G, c.B);
            IDropShadow_get_Offset(shadow, &v3); printf("  Offset %g %g %g\n", v3.X, v3.Y, v3.Z);
            f = -1; IDropShadow_get_Opacity(shadow, &f); printf("  Opacity %g\n", f);
            hr = IDropShadow_get_Mask(shadow, &mask); printf("  Mask %#lx %p\n", hr, mask);
            if (SUCCEEDED(IDropShadow_QueryInterface(shadow, &IID_IDropShadow2, (void **)&shadow2)))
            {
                IDropShadow2_get_SourcePolicy(shadow2, &policy); printf("  SourcePolicy %d\n", policy);
                IDropShadow2_Release(shadow2);
            }
            hr = IDropShadow_put_BlurRadius(shadow, -1.0f); printf("  put_BlurRadius(-1) %#lx\n", hr);
            hr = IDropShadow_put_Opacity(shadow, 2.0f); printf("  put_Opacity(2) %#lx\n", hr);
            f = -1; IDropShadow_get_Opacity(shadow, &f); printf("  Opacity %g\n", f);
            printf("  BlurRadius animation %#lx\n", start(shadow, L"BlurRadius", scalar_frame_animation(compositor)));
            printf("  Color expression %#lx\n", 0L);
            IDropShadow_QueryInterface(shadow, &IID_ICompositionShadow, (void **)&cshadow);
            ISpriteVisual_QueryInterface(sprite, &IID_ISpriteVisual2, (void **)&sprite2);
            hr = ISpriteVisual2_put_Shadow(sprite2, cshadow);
            printf("  put_Shadow %#lx\n", hr);
            ISpriteVisual2_Release(sprite2);
            ICompositionShadow_Release(cshadow);
            IDropShadow_Release(shadow);
        }
    }

    printf("done\n");
    return 0;
}
