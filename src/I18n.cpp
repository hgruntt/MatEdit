#include "I18n.h"

#include <unordered_map>

namespace {
std::string g_language = "en";

const std::unordered_map<std::string, std::string> ru = {
    {"NO %s FILE SELECTED", "ФАЙЛ %s НЕ ВЫБРАН"},
    {"CHANGE", "ИЗМЕНИТЬ"}, {"RELOAD", "ПЕРЕЗАГРУЗИТЬ"},
    {"Find...", "Найти..."}, {"Delete texture?", "Удалить текстуру?"},
    {"DELETE", "УДАЛИТЬ"}, {"CANCEL", "ОТМЕНА"},
    {"SWAP", "ПОМЕНЯТЬ"}, {"FIT", "ВПИСАТЬ"},
    {"WAD", "WAD"}, {"TEXTURE", "ТЕКСТУРА"}, {"DDS", "DDS"},
    {"DEF", "DEF"}, {"FOLDER", "ПАПКА"}, {"CLICK TO LOAD", "НАЖМИТЕ, ЧТОБЫ ЗАГРУЗИТЬ"},
    {"PRESS A KEY...", "НАЖМИТЕ КЛАВИШУ..."},
    {"NEW MATERIAL", "НОВЫЙ МАТЕРИАЛ"}, {"CREATE", "СОЗДАТЬ"},
    {"ASSIGN SELECTED TEXTURE AS", "НАЗНАЧИТЬ ВЫБРАННУЮ ТЕКСТУРУ КАК"},
    {"DIFFUSE", "ДИФФУЗНАЯ"},
    {"NORMAL", "НОРМАЛИ"},
    {"GLOSS", "ГЛЯНЕЦ"},
    {"LUMA", "СВЕЧЕНИЕ"},
    {"BUMP", "РЕЛЬЕФ"},
    {"DETAIL", "ДЕТАЛИЗАЦИЯ"},
    {"CLICK A TEXTURE", "ВЫБЕРИТЕ ТЕКСТУРУ"},
    {"PREVIEW UNAVAILABLE", "ПРЕДПРОСМОТР НЕДОСТУПЕН"},
    {"LOAD A .MAT FILE TO ASSIGN TEXTURES", "ЗАГРУЗИТЕ ФАЙЛ .MAT, ЧТОБЫ НАЗНАЧИТЬ ТЕКСТУРЫ"},
    {"NO SEARCH RESULTS", "НЕТ РЕЗУЛЬТАТОВ ПОИСКА"},
    {"File already exists.", "Файл уже существует."},
    {"REPLACE", "ЗАМЕНИТЬ"}, {"SAVE TEXTURE", "СОХРАНИТЬ ТЕКСТУРУ"},
    {"SAVE", "СОХРАНИТЬ"}, {"PLACES", "РАСПОЛОЖЕНИЯ"},
    {"GAME ROOT", "КОРНЕВАЯ ПАПКА ИГРЫ"}, {"TEXTURES", "ТЕКСТУРЫ"},
    {"THIS FOLDER IS EMPTY", "ЭТА ПАПКА ПУСТА"},
    {"FILE NAME", "ИМЯ ФАЙЛА"},
    {"The selected folder must be inside the game root.", "Выбранная папка должна находиться внутри папки игры."},
    {"OK", "ОК"}, {"NORMAL MAP", "КАРТА НОРМАЛЕЙ"},
    {"RESET SETTINGS", "СБРОСИТЬ НАСТРОЙКИ"}, {"GLOSS MAP", "КАРТА ГЛЯНЦА"},
    {"BUMP MAP", "КАРТА РЕЛЬЕФА"},
    {"Visual Materials (.mat)", "Визуальные материалы (.mat)"},
    {"NO .MAT FILES FOUND", "ФАЙЛЫ .MAT НЕ НАЙДЕНЫ"},
    {"+ NEW MATERIAL", "+ НОВЫЙ МАТЕРИАЛ"}, {"NO MATERIALS FOUND", "МАТЕРИАЛЫ НЕ НАЙДЕНЫ"},
    {"MATERIAL NAME", "ИМЯ МАТЕРИАЛА"}, {"Diffuse", "Диффуз"},
    {"Normal", "Нормали"}, {"Gloss", "Глянец"}, {"Luma", "Свечение"},
    {"Bump", "Рельеф"}, {"Detail", "Детали"},
    {"Smoothness", "Гладкость"}, {"Reflect", "Отражение"},
    {"Relief", "Рельеф"}, {"Refract", "Преломление"},
    {"Abberation", "Аберрация"}, {"Texture Tiling", "Масштаб текстуры"},
    {"Symmetric", "Симметрично"}, {"Phys Material", "Физический материал"},
    {"Apply Changes", "Применить изменения"}, {"Save All", "Сохранить всё"},
    {"Show Model", "Показать модель"}, {"Model Shape", "Форма модели"},
    {"Normal Map", "Карта нормалей"}, {"Gloss Map", "Карта глянца"},
    {"Luma Map", "Карта свечения"}, {"Use Bump", "Использовать рельеф"},
    {"Light Mode", "Режим освещения"}, {"Dynamic Speed", "Скорость динамики"},
    {"Dynamic Radius", "Радиус динамики"}, {"Intensity", "Интенсивность"},
    {"Color", "Цвет"}, {"Physical Materials (.def)", "Физические материалы (.def)"},
    {"Select Physical Material", "Выберите физический материал"},
    {"Material Name", "Имя материала"}, {"Add New Def Entry", "Добавить запись DEF"},
    {"No physical materials loaded", "Физические материалы не загружены"},
    {"Load / Create Default", "Загрузить / создать по умолчанию"},
    {"Impact Decal", "След от удара"}, {"Impact Parts", "Частицы удара"},
    {"Impact Sound", "Звук удара"}, {"Step Sound", "Звук шага"},
    {"Apply Def Changes", "Применить изменения DEF"},
    {"Save materials.def", "Сохранить materials.def"},
    {"Project", "Проект"}, {"Load All WADs", "Загрузить все WAD"},
    {"Reload Saved WADs", "Перезагрузить сохранённые WAD"},
    {"Clear Loaded WADs", "Очистить загруженные WAD"},
    {"Auto-Gen", "Автогенерация"}, {"Quick Create .MAT", "Быстро создать .MAT"},
    {"Quick Create .DEF", "Быстро создать .DEF"}, {"Settings", "Настройки"},
    {"Open Settings", "Открыть настройки"}, {"Panels", "Панели"},
    {"File Browser", "Обозреватель файлов"}, {"Texture Preview", "Просмотр текстуры"},
    {"Material Editor", "Редактор материалов"}, {"Show All Panels", "Показать все панели"},
    {"Hide All Panels", "Скрыть все панели"}, {"Instruments", "Инструменты"},
    {"Reset Material View", "Сбросить вид материала"},
    {"Reset Lighting", "Сбросить освещение"},
    {"Reload Current Material", "Перезагрузить текущий материал"},
    {"Clear Texture Assignments", "Очистить назначенные текстуры"},
    {"Help", "Справка"}, {"About MatEdit", "О MatEdit"},
    {"Searches game files, material files, model textures and loaded WAD textures. Type part of a name or path.",
     "Ищет файлы игры, материалы, текстуры моделей и загруженные WAD. Введите часть имени или пути."},
    {"SCENE", "СЦЕНА"}, {"MAT", "MAT"}, {"AUTO-GEN", "АВТОГЕНЕРАЦИЯ"},
    {"Creates a PrimeXT texture material in scripts/*.mat.", "Создаёт материал текстуры PrimeXT в scripts/*.mat."},
    {"File", "Файл"}, {"MAT file name. The file is created inside scripts/. The .mat extension is added automatically if missing.",
     "Имя MAT-файла. Файл создаётся в scripts/. Расширение .mat добавится автоматически, если его нет."},
    {"Texture / model texture", "Текстура / текстура модели"},
    {"wall1 or model/body", "wall1 или model/body"},
    {"Base texture name or model texture reference used by the material. Keep it identical to the texture name in the WAD or model.",
     "Имя основной текстуры или ссылка на текстуру модели. Оно должно совпадать с именем текстуры в WAD или модели."},
    {"Tip: the texture name must match the WAD/model texture. Extra maps can use PrimeXT suffixes such as _norm and _gloss.",
     "Имя текстуры должно совпадать с текстурой в WAD/модели. Для дополнительных карт используются суффиксы PrimeXT, например _norm и _gloss."},
    {"Physical Material", "Физический материал"},
    {"Physical material type written to the MAT definition, such as concrete, wood or metal.",
     "Тип физического материала в MAT, например concrete, wood или metal."},
    {"Create & Open", "Создать и открыть"}, {"Cancel", "Отмена"},
    {"Creates a PrimeXT physical-material definition in scripts/*.def.",
     "Создаёт определение физического материала PrimeXT в scripts/*.def."},
    {"Material name", "Имя материала"},
    {"Name of the physical material definition. This is the name used by the game when resolving the material type.",
     "Имя определения физического материала, которое игра использует для выбора типа материала."},
    {"Impact decal", "След от удара"},
    {"Decal name used when a bullet or impact hits this material.", "Имя декали при попадании пули или удара по материалу."},
    {"Impact sounds", "Звуки удара"},
    {"Space-separated impact sound paths. Up to 8 sounds are written to the DEF file.",
     "Пути к звукам удара через пробел. В DEF-файл записывается до 8 звуков."},
    {"Step sounds", "Звуки шагов"},
    {"Space-separated footstep sound paths. Up to 8 sounds are written to the DEF file.",
     "Пути к звукам шагов через пробел. В DEF-файл записывается до 8 звуков."},
    {"Tip: sound paths are relative to sound/. Up to 8 impact/step sounds are supported.",
     "Пути к звукам задаются относительно sound/. Поддерживается до 8 звуков удара и шагов."},
    {"SKYBOX DIRECTORY NOT FOUND: gfx/env", "ПАПКА СКАЙБОКСА НЕ НАЙДЕНА: gfx/env"},
    {"General", "Общие"}, {"Game Root", "Папка игры"},
    {"Anti-Aliasing", "Сглаживание"}, {"MSAA Samples", "Образцы MSAA"},
    {"Field of View", "Поле зрения"}, {"Display", "Экран"},
    {"Show FPS", "Показывать FPS"}, {"Texture Filtering", "Фильтрация текстур"},
    {"Auto-assign material textures", "Автоматически назначать текстуры материала"},
    {"Allow Light Intensity > 5", "Разрешить интенсивность света > 5"},
    {"Viewport", "Область просмотра"}, {"Unlimited Zoom In", "Неограниченное приближение"},
    {"WAD Loading", "Загрузка WAD"}, {"Load all WAD files", "Загружать все файлы WAD"},
    {"Keybinds", "Клавиши"}, {"Controls", "Управление"},
    {"Mouse orbit, pan and wheel zoom use the mouse and are not remappable.",
     "Вращение, перемещение и масштабирование колёсиком выполняются мышью и не переназначаются."},
    {"VSync", "Вертикальная синхронизация"},
    {"Themes", "Темы"}, {"Create Theme", "Создать тему"}, {"Apply", "Применить"},
    {"Delete Custom", "Удалить пользовательскую"}, {"Theme Name", "Имя темы"},
    {"Create", "Создать"}, {"Select a custom theme to edit colors.", "Выберите пользовательскую тему для изменения цветов."},
    {"Skybox", "Скайбокс"}, {"None", "Нет"}, {"Save Settings", "Сохранить настройки"},
    {"MatEdit", "MatEdit"},
    {"Material Editor for PrimeXT and similar projects running on Xash3D / Xash3D FWGS.",
     "Редактор материалов PrimeXT и похожих проектов на Xash3D / Xash3D FWGS."},
    {"Created for editing game materials, textures and related material definitions used by these projects.",
     "Для редактирования игровых материалов, текстур и соответствующих определений материалов."},
    {"Author: hgruntt", "Автор: hgruntt"}, {"License: GPL-3.0", "Лицензия: GPL-3.0"},
    {"MatEdit is absolutely free. If somebody charged you money for this program, you were scammed.",
     "MatEdit полностью бесплатен. Если с вас взяли деньги за программу, вас обманули."},
    {"Third-party components include Dear ImGui, GLFW, GLM, GLI, GLAD and stb_image, each distributed under its respective license.",
     "Сторонние компоненты: Dear ImGui, GLFW, GLM, GLI, GLAD и stb_image, каждый распространяется по собственной лицензии."},
    {"PROJECT CONTRIBUTORS", "УЧАСТНИКИ ПРОЕКТА"},
    {"TEXTURE GENERATOR", "ГЕНЕРАТОР ТЕКСТУР"},
    {"Contributor list is not available yet.", "Список участников пока недоступен."},
    {"Language", "Язык"}, {"English", "Английский"}, {"Russian", "Русский"},
    {"Search files / WAD textures...", "Поиск файлов / текстур WAD..."}
    ,{"Parameters & Model", "Параметры и модель"}
    ,{"Parameters & Models", "Параметры и модели"}
    ,{"Lighting & Maps", "Освещение и карты"}
    ,{"Physical Material Entries", "Список физических материалов"}
    ,{"Parameters Editor", "Параметры материала"}
    ,{"MAT FILE", "ФАЙЛ MAT"}, {"MATERIAL", "МАТЕРИАЛ"}
    ,{"SELECT .MAT FILE", "ВЫБЕРИТЕ ФАЙЛ .MAT"}
    ,{"Search .mat files...", "Поиск файлов .mat..."}
    ,{"Search material...", "Поиск материала..."}
    ,{"SOURCE", "ИСТОЧНИК"}, {"NORMAL PREVIEW", "ПРЕДПРОСМОТР НОРМАЛЕЙ"}
    ,{"GLOSS PREVIEW", "ПРЕДПРОСМОТР ГЛЯНЦА"}, {"BUMP PREVIEW", "ПРЕДПРОСМОТР РЕЛЬЕФА"}
    ,{"Show texture in the viewport.", "Показывать текстуру в окне сцены."}
    ,{"Hide texture in the viewport.", "Скрывать текстуру в окне сцены."}
    ,{"Height channel", "Канал высоты"}
    ,{"Select which source channel is interpreted as height. Luminance combines RGB.", "Выберите канал исходной текстуры для высоты. Яркость рассчитывается из RGB."}
    ,{"Invert height", "Инвертировать высоту"}
    ,{"Swap raised and recessed areas before calculating normals.", "Поменять возвышенности и углубления перед расчётом нормалей."}
    ,{"Black point", "Чёрная точка"}
    ,{"Source values at or below this level become the minimum height.", "Значения ниже этого уровня станут минимальной высотой."}
    ,{"White point", "Белая точка"}
    ,{"Source values at or above this level become the maximum height.", "Значения выше этого уровня станут максимальной высотой."}
    ,{"Smoothing", "Сглаживание"}
    ,{"Blend toward a Gaussian blur before calculating normals to reduce speckle.", "Смешать текстуру с размытием по Гауссу для уменьшения шума."}
    ,{"Sharpening", "Резкость"}
    ,{"Increase local source contrast before deriving height; this can amplify noise.", "Увеличить локальный контраст перед расчётом высоты; это может усилить шум."}
    ,{"HEIGHT INPUT", "ВЫСОТА ИСТОЧНИКА"}
    ,{"NORMAL SETTINGS", "ПАРАМЕТРЫ НОРМАЛЕЙ"}
    ,{"Strength", "Сила"}
    ,{"Controls how strongly height changes tilt the surface. Zero produces a flat normal map.", "Определяет наклон поверхности по высоте. Ноль создаёт плоскую карту нормалей."}
    ,{"Gradient filter", "Фильтр градиента"}
    ,{"Choose how neighboring height samples are converted into X/Y surface slopes.", "Выберите способ вычисления наклона поверхности X/Y по соседним значениям высоты."}
    ,{"Wrap edges", "Замыкать края"}
    ,{"Wrap sampling across image borders to avoid seams on repeating textures.", "Замыкать выборку через границы изображения, чтобы избежать швов на повторяющихся текстурах."}
    ,{"Flip X", "Инвертировать X"}
    ,{"Reverse the red-channel direction to match the model tangent-space convention.", "Изменить направление красного канала согласно касательному пространству модели."}
    ,{"Flip Y", "Инвертировать Y"}
    ,{"Reverse the green-channel direction; commonly needed for OpenGL/DirectX convention changes.", "Изменить направление зелёного канала; часто требуется при переходе между OpenGL и DirectX."}
    ,{"Full-range Z", "Полный диапазон Z"}
    ,{"Change blue-channel encoding; leave off for the usual remapped normal encoding.", "Изменить кодирование синего канала; обычно оставляют выключенным."}
    ,{"OUTPUT", "ВЫВОД"}
    ,{"Mipmaps", "Мип-текстуры"}
    ,{"Write smaller filtered levels for distant or minified surfaces.", "Создавать уменьшенные уровни для удалённых и уменьшенных поверхностей."}
    ,{"DDS format", "Формат DDS"}
    ,{"BC5 stores two tangent-space directions efficiently; BC7 also retains the blue channel.", "BC5 эффективно хранит два направления касательного пространства; BC7 сохраняет также синий канал."}
    ,{"Central difference", "Центральная разность"}
    ,{"Luminance", "Яркость"}, {"Red channel", "Красный канал"}
    ,{"Green channel", "Зелёный канал"}, {"Blue channel", "Синий канал"}
    ,{"Alpha channel", "Альфа-канал"}
    ,{"Gloss source", "Источник глянца"}
    ,{"Choose luminance or a source channel to use as the gloss mask.", "Выберите яркость или канал исходной текстуры для маски глянца."}
    ,{"Minimum level", "Минимальный уровень"}
    ,{"Values below this become black when normalization is enabled.", "При нормализации значения ниже этого уровня станут чёрными."}
    ,{"Maximum level", "Максимальный уровень"}
    ,{"Values above this become white when normalization is enabled.", "При нормализации значения выше этого уровня станут белыми."}
    ,{"Normalize range", "Нормализовать диапазон"}
    ,{"Normalize", "Нормализовать"}
    ,{"Stretch the selected minimum/maximum range across the full gloss range.", "Растянуть выбранный диапазон на весь диапазон глянца."}
    ,{"Softness", "Мягкость"}
    ,{"Blend the linear response toward smoothstep.", "Смешать линейную кривую со сглаженной."}
    ,{"ADJUSTMENTS", "КОРРЕКЦИЯ"}
    ,{"Contrast", "Контраст"}
    ,{"Expand or compress values around middle gray.", "Расширить или сжать значения относительно среднего серого."}
    ,{"Brightness", "Яркость"}
    ,{"Add or subtract a constant from every gloss value.", "Прибавить или вычесть константу из каждого значения глянца."}
    ,{"Gamma", "Гамма"}
    ,{"Below 1 brightens midtones; above 1 darkens them.", "Значение ниже 1 осветляет средние тона, выше 1 — затемняет."}
    ,{"Increase local source contrast before extracting gloss; this can amplify noise.", "Увеличить локальный контраст перед извлечением глянца; это может усилить шум."}
    ,{"Invert mask", "Инвертировать маску"}
    ,{"Swap matte and glossy regions after extracting the selected source.", "Поменять матовые и глянцевые области после извлечения источника."}
    ,{"BC4 is compact for a single-channel gloss mask; BC7 stores it in RGBA.", "BC4 компактен для одноканальной маски глянца; BC7 хранит её в RGBA."}
    ,{"Red (PrimeXT)", "Красный (PrimeXT)"}
    ,{"PrimeXT reads _hmap from its red channel. Select another channel only if the source stores height elsewhere.", "PrimeXT читает _hmap из красного канала. Выбирайте другой канал, только если высота хранится иначе."}
    ,{"Swap raised and recessed areas.", "Поменять возвышенности и углубления."}
    ,{"Adjust the distribution of height values after level remapping. 1.0 leaves it unchanged.", "Изменить распределение высоты после коррекции уровней. 1.0 оставляет его без изменений."}
    ,{"Blend toward a Gaussian blur to reduce small height variations.", "Смешать с размытием по Гауссу для уменьшения мелких перепадов высоты."}
    ,{"Increase local source contrast before extracting height; this can amplify noise.", "Увеличить локальный контраст перед извлечением высоты; это может усилить шум."}
    ,{"Expand or compress heights around middle gray.", "Расширить или сжать высоту относительно среднего серого."}
    ,{"Raise or lower the whole height range.", "Повысить или понизить весь диапазон высоты."}
    ,{"Stretch the remaining range to black and white. Off preserves authored PrimeXT _hmap values.", "Растянуть остаточный диапазон от чёрного до белого. При выключении сохраняются значения PrimeXT _hmap."}
    ,{"Wrap the smoothing filter across image borders for repeating textures.", "Замыкать фильтр сглаживания через границы повторяющихся текстур."}
    ,{"BC4 is compact for single-channel height; BC7 stores gray values in RGBA.", "BC4 компактен для одноканальной высоты; BC7 хранит оттенки серого в RGBA."}
    ,{"Cube", "Куб"}, {"Sphere", "Сфера"}, {"Plane", "Плоскость"}
    ,{"Cylinder", "Цилиндр"}, {"Cone", "Конус"}, {"Torus", "Тор"}
    ,{"Newell Teapot", "Чайник Ньюэлла"}, {"Camera", "Камера"}
    ,{"Fixed", "Фиксированный"}, {"Dynamic", "Динамический"}
    ,{"Toggle Panels", "Показать/скрыть панели"}, {"Free Camera", "Свободная камера"}
    ,{"Move Forward", "Вперёд"}
    ,{"Move Backward", "Назад"}, {"Move Left", "Влево"}, {"Move Right", "Вправо"}
    ,{"Move Up", "Вверх"}, {"Move Down", "Вниз"}, {"Fast Move", "Ускорение"}
    ,{"Toggle Model", "Показать/скрыть модель"}
    ,{"ImGui", "ImGui"}, {"Pastel Pink", "Пастельно-розовая"}
    ,{"Pastel Green", "Пастельно-зелёная"}, {"AMOLED", "AMOLED"}
    ,{"Window", "Окно"}, {"Popup", "Всплывающие окна"}
    ,{"Fields", "Поля"}, {"Fields Hover", "Поля при наведении"}
    ,{"Fields Active", "Активные поля"}, {"Buttons", "Кнопки"}
    ,{"Button Hover", "Кнопки при наведении"}, {"Button Active", "Активные кнопки"}
    ,{"Text", "Текст"}, {"Text Disabled", "Неактивный текст"}
    ,{"Accent", "Акцент"}, {"Border", "Граница"}, {"Title", "Заголовок"}
    ,{"Title Active", "Активный заголовок"}, {"Menu Bar", "Строка меню"}
    ,{"Scroll Bar", "Полоса прокрутки"}, {"Scroll Grab", "Ползунок прокрутки"}
    ,{"Header", "Заголовок секции"}, {"Header Hover", "Секция при наведении"}
    ,{"Header Active", "Активная секция"}, {"Separator", "Разделитель"}
    ,{"Resize Grip", "Маркер изменения размера"}, {"Tab", "Вкладка"}
    ,{"Tab Hover", "Вкладка при наведении"}, {"Tab Active", "Активная вкладка"}
    ,{"Selected Text", "Выделенный текст"}, {"Drag & Drop", "Перетаскивание"}
    ,{"Navigation", "Навигация"}, {"Table Header", "Заголовок таблицы"}
    ,{"Table Border", "Граница таблицы"}, {"Table Row", "Строка таблицы"}
    ,{"Table Alt Row", "Чередующаяся строка"}, {"Plot", "График"}
    ,{"Plot Hover", "График при наведении"}, {"Viewport BG", "Фон области просмотра"}
    ,{"Custom", "Пользовательская"}, {" Custom", " Пользовательская"}
    ,{"No skyboxes found. Expected six files in gfx/env: namebk, namelf, namert, nameft, nameup, namedn.", "Скайбоксы не найдены. В gfx/env ожидаются шесть файлов: namebk, namelf, namert, nameft, nameup, namedn."}
    ,{"FPS: %.1f", "FPS: %.1f"}
};

std::unordered_map<std::string, std::string> translatedIds;
}

const char* Tr(const char* text) {
    if (!text || g_language != "ru") return text;
    const std::string source(text);
    const std::size_t idStart = source.find("##");
    const std::string visible = source.substr(0, idStart);
    const auto translation = ru.find(visible);
    if (translation == ru.end()) return text;
    if (idStart == std::string::npos) return translation->second.c_str();
    auto result = translatedIds.emplace(source, translation->second + source.substr(idStart));
    return result.first->second.c_str();
}

void SetLanguage(const std::string& language) {
    g_language = language == "ru" ? "ru" : "en";
}
