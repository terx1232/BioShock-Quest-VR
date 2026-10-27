// BioshockVR.ini next to the game exe.
#pragma once
#include "vrmath.h"

struct Config {
    bool enabled = true;           // master switch, off = plain d3d9 passthrough
    bool stereo = true;            // alternate eye rendering
    bool positional = true;        // 6DoF: head position moves the camera
    float unitsPerMetre = 77.0f;   // единиц Unreal в метре (глаза Джека на 128 ед. от пола = 1.66 м)
    float ipdScale = 1.0f;         // multiplies the headset IPD (bigger = world looks smaller)
    float headsetFov = 100.0f;     // поле зрения шлема по вертикали (Quest 2), градусы
    float fovOverride = 0.0f;      // > 0 forces this horizontal FOV instead of the headset's
    int fovOffset = 0x420;         // APlayerController::FovAngle (verified at runtime)
    bool logPoses = false;         // log every 90th pose
    float turnSpeed = 120.0f;      // плавный поворот правым стиком, градусы в секунду
    bool snapTurn = false;         // поворот рывками вместо плавного
    float snapAngle = 30.0f;       // угол рывка, градусы
    int menuFrames = 20;
    bool noBob = true;
    bool bobLog = false;               // без покачивания камеры при ходьбе
    bool gpuTiming = false;          // замер времени видеочипа в лог
    bool cpuTiming = false;          // separate engine, driver wait, limiter and input costs
    bool rawPose = true;             // full precision bridge; off is the legacy protocol A/B path
    float gameFpsCap = 72.0f;        // >0: предел кадров игры в VR (видеочип остаётся WinlatorXR)
    bool syncPixel = true;           // пиксель синхронизации в кадре (0 - отладка: шлем показывает кадр с новейшей позой)
    bool syncFromCamera = true;      // пиксель синхронизации - номер позы камеры этого кадра
    float pointerMinCutoff = 0.8f;  // сглаживание луча меню: частота среза в покое, Гц
    float pointerBeta = 3.0f;       // и её рост со скоростью луча
    float theaterFov = 0.0f;
    float menuMouseScale = 1.47f;  // чувствительность мыши игры: во сколько раз она умножает смещения
    float menuMouseScaleX = 1.92f, menuMouseScaleY = 1.44f; // measured UI gain at 1280x720
    bool weaponInHand = true;      // руки и оружие игры в правом контроллере
    bool plasmidInHand = false;
    bool rigidWeapon = true;       // предмет в руке жёстко в контроллере, без анимаций рук игры
    bool noWeaponAnimations = false;
    float wrenchScale = 1;
    float wrenchForward = 0;        // metres along controller aim; wrench only
    bool meleePitchServo = false;
    bool weaponGripAnchor = false; // anchor held model at the hand rather than the flat-game animation offset
    vr::Vec3 weaponGripOffset = {}; // grip point in the held model's own coordinates, Unreal units
    bool hideCrosshair = false;    // native bReticleDisabled; keep prompts/subtitles and wrist HUD
    bool aimReticles = false;      // controller-directed markers at current world depth
    bool weaponGeometry = false;   // independent geometry stereo for foreground mesh draws
    float weaponNearScale = .1f;   // smaller near plane in the weapon layer only
    float weaponPitch = -40.0f;    // наклон оружия относительно рукоятки, градусы
    vr::Vec3 weaponOffset = {22.0f, 9.0f, -14.0f};  // где правая кисть в модели рук (вперёд, вправо, вверх; ед. Unreal)
    bool vignette = false;         // затемнение краёв кадра в игре
    bool gameHands = true;         // контроллеры Quest 2 в руках в игре
    bool mouseInject = true;       // отладка: 0 = не трогать мышь игры в кинотеатре
    bool theaterDraw = true;       // отладка: 0 = не рисовать кинотеатр (виден кадр игры)
    float debugClickU = -1, debugClickV = -1;  // отладка: щёлкать в эту точку экрана меню (0..1)
    float debugClickPeriod = 3;    // раз в столько секунд
    float menuAspect = 0.0f;       // >0: в меню экран движка сужается до таких пропорций (для квадратного кадра)
    bool depthStereo = false;      // стерео по буферу глубины: кадр двойной ширины, оба глаза из одного кадра
    bool lazyDepth = false;         // keep the game's original depth in menus; capture only in gameplay
    unsigned gameWidth = 0, gameHeight = 0;  // размер картинки движка в игре, если окно больше (0 = всё окно)
    unsigned worldBackWidth = 0, worldBackHeight = 0; // optional game-only TEMPSETRES; menus keep startup size
    bool worldWindowed = false;    // exact game buffer without fullscreen mode rounding
    float screenDistance = 3.5f;   // расстояние до экрана кинотеатра, м
    bool dynamicRes = true;        // разрешение картинки движка подстраивается, чтобы держать частоту шлема
    unsigned dynamicMin = 640;     // нижняя граница динамического разрешения (сторона квадрата)
    bool depthDebug = false;       // отладка: вместо глаз показать буфер глубины
    float depthStrength = 1.0f;    // сила стерео (1 = как у реальных глаз)
    float depthMaxShift = 0.035f;  // предельный сдвиг точки, доля ширины глаза (самые близкие предметы)
    float depthNearSoftness = 0;   // fraction of max disparity reserved for a smooth near-depth knee (0 = legacy)
    float screenWidthDeg = 80.0f;  // ширина экрана кинотеатра, градусы
    bool roomScale = true;         // шаги по комнате двигают персонажа (со стенами и ступеньками)
    bool realHeight = true;        // камера на высоте глаз игрока над полом (по шлему)
    float playerHeight = 1.75f;    // рост игрока, м (если шлем не сообщает высоту над полом)
    bool debugDump = false;        // отладка: разобрать устройство классов игры в лог
    bool devHarness = false;       // file commands and forced test input; off in normal builds
    bool hudInMenu = false;        // отладка: HUD в свою текстуру и в меню
    bool menuHands = false;        // руки в меню (кинотеатре); по умолчанию только луч
    bool menuStereo = false;       // stereo cinema independent of gameplay depth availability
    bool nativeCinema = false;     // Quest compositor quad plus current-pose white beam
    bool screenMipmaps = true;     // optional minification quality; disable for lighter Quest menus
    bool menuProps = false;        // руки в меню - ключ и бур Большого Папочки вместо контроллеров
    bool hideArms = true;
    unsigned hideMask = 0;
    unsigned armsDrawSite = 0x10b917b4;  // вызов отрисовки скелетных моделей (руки) в Bioshock.exe
    unsigned armsVertices[8] = {};       // число вершин частей сетки рук (их не рисуем)         // отладка: бит bHidden (0 = по умолчанию 0x2)          // руки Джека скрыты, в правой руке только оружие
    bool plasmidRay = true;        // луч-прицел плазмида из левой руки
    bool flatMenus = false;        // меню/пауза/загрузки - плоский экран WinlatorXR (полное разрешение шлема)
    float hudScale = 1.5f;         // размер интерфейса игры (панель на руке, подсказки перед глазами)
    bool wristHud = true;
    bool gameplayHud = false;      // source-correct left/right wrist + complete text plane
    bool hudDashboard = false, hudVrText = false;
    float hudSupersample = 1;
    float navigationDistance = 2.f;
    bool hideAttackTutorial = false; // only FireWeapon (wrench attack text + button image)
    bool hideDamageOverlay = false; // native received-hit visual event, not damage calculation
    bool ammoOnLeft = false;
    bool questHealingButtons = false;
    bool navigationGeometry = false;
    bool worldPosePin = false;     // negotiated Android retention of the displayed image pose
    bool swingMelee = true;        // гаечный ключ бьёт при взмахе правой рукой
    float swingSpeed = 2.0f;       // скорость взмаха, м/с          // показатели HUD на левом запястье, подсказки перед глазами
    bool hudMessagesOnLeft = false; // upper quest/important-message band follows the left wrist
    bool questHackGrip = false;
    bool instantEve = false;
    bool questControllerBindings = false;
};

extern Config g_config;
void LoadConfig(const char* path);
