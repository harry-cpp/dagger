#include "core/graphics/animations.h"
#include "core/graphics/animation.h"
#include "core/engine.h"
#include "core/graphics/sprite.h"

ViewPtr<Animation> AnimationSystem::Get(String name_)
{
    auto animation = Engine::Res<Animation>()[name_];
    assert(animation != nullptr);
    return animation;
}

#if !defined(NDEBUG)
void AnimationSystem::RenderToolMenu()
{
    if (ImGui::BeginMenu("Animations"))
    {
        if (ImGui::MenuItem("Reload All"))
        {
            LoadDefaultAssets();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Stop All"))
        {
            auto& entities = Engine::Registry().view<Animator>();
            entities.each([](Animator& anim_)
                {
                    anim_.animationPlaying = false;
                });
        }

        if (ImGui::MenuItem("Play All"))
        {
            auto& entities = Engine::Registry().view<Animator>();
            entities.each([](Animator& anim_)
                {
                    anim_.animationPlaying = true;
                });
        }

        ImGui::EndMenu();
    }
}
#endif //!defined(NDEBUG)

void AnimationSystem::OnLoadAsset(AssetLoadRequest<Animation> request_)
{
    FilePath path(request_.path);

    if (!Files::exists(path))
    {
        Engine::Dispatcher().trigger<Error>(Error{ fmt::format("Couldn't load animation from {}.", request_.path) });
        return;
    }

    FileInputStream handle;
    auto absolutePath = Files::absolute(path);
    handle.open(absolutePath);

    if (!handle.is_open())
    {
        Engine::Dispatcher().trigger<Error>(Error{ fmt::format("Couldn't open animation file '{}' for reading.", absolutePath.string()) });
        return;
    }

    JSON::json json;
    handle >> json;

    Animation* animation = new Animation();

    assert(json.contains("animation-name"));
    animation->name = json["animation-name"];

    if (json.contains("animation-length-ms"))
    {
        animation->length = (UInt32)json["animation-length-ms"];
    }
    else
    {
        animation->length = 1000;
    }

    assert(animation->length > 0.0);
    animation->absoluteLength = animation->length / 1000.0;

    animation->frameLengthRelativeSum = 0;

    assert(json.contains("animation-frames"));
    for (auto& sub : json["animation-frames"])
    {
        Frame frame;

        assert(sub.contains("texture-name"));
        frame.textureName = sub["texture-name"];

        if (sub.contains("pivot"))
        {
            if (sub["pivot"].is_array())
            {
                frame.pivot = { sub["pivot"][0], sub["pivot"][1] };
            }
            else
            {
                String pivot = sub["pivot"];

                if (pivot == "CENTER")
                    frame.pivot = { 0.0f, 0.0f };
                else if (pivot == "BOTTOM")
                    frame.pivot = { 0.0f, 0.5f };
                else if (pivot == "TOP")
                    frame.pivot = { 0.0f, -0.5f };
            }
        }

        if (sub.contains("relative-length"))
        {
            frame.relativeLength = sub["relative-length"];
            assert(frame.relativeLength > 0);
        }
        else
            frame.relativeLength = 1;

        animation->frameLengthRelativeSum += frame.relativeLength;

        for (auto& frame : animation->frames)
        {
            frame.absoluteLength = animation->absoluteLength *
                ((Float64)frame.relativeLength / (Float64)animation->frameLengthRelativeSum);
        }

        frame.texture = Engine::Res<Texture>()[frame.textureName];
        animation->frames.push_back(std::move(frame));
    }

    auto& library = Engine::Res<Animation>();
    if (library.contains(animation->name))
    {
        delete library[animation->name];
    }

    library[animation->name] = animation;
    Logger::info("Animation '{}' loaded!", animation->name);
}

void AnimationSystem::LoadDefaultAssets()
{
    for (auto& entry : Files::recursive_directory_iterator("animations"))
    {
        auto path = entry.path().string();
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            Engine::Dispatcher().trigger<AssetLoadRequest<Animation>>(AssetLoadRequest<Animation>{ path });
        }
    }
}

void AnimationSystem::SpinUp()
{
    Engine::Dispatcher().sink<AssetLoadRequest<Animation>>().connect<&AnimationSystem::OnLoadAsset>(this);
#if !defined(NDEBUG)
    Engine::Dispatcher().sink<ToolMenuRender>().connect<&AnimationSystem::RenderToolMenu>(this);
#endif //!defined(NDEBUG)
    LoadDefaultAssets();
}

void AnimationSystem::Run()
{
    auto& entities = Engine::Registry().view<Animator, Sprite>();
    entities.each([](Animator& anim_, Sprite& sprite_) 
        {
            if (anim_.animationPlaying)
            {
                const auto currentAnimation = AnimationSystem::Get(anim_.currentAnimation);
                const auto& frame = currentAnimation->frames[anim_.currentFrame];

                anim_.currentFrameTime += Engine::DeltaTime();
                if (anim_.currentFrameTime > frame.absoluteLength)
                {
                    UInt32 count = currentAnimation->frames.size();
                    anim_.currentFrame = (anim_.currentFrame + 1) % count;
                    anim_.currentFrameTime = 0.0;
                    
                    AssignSpriteTexture(sprite_, currentAnimation->frames[anim_.currentFrame].textureName);
                }
            }
        });
}

void AnimationSystem::WindDown()
{
    auto& library = Engine::Res<Animation>();
    for (auto [_, value] : library)
    {
        delete value;
    }

    Engine::Dispatcher().sink<AssetLoadRequest<Animation>>().disconnect<&AnimationSystem::OnLoadAsset>(this);
#if !defined(NDEBUG)
    Engine::Dispatcher().sink<ToolMenuRender>().disconnect<&AnimationSystem::RenderToolMenu>(this);
#endif //!defined(NDEBUG)
}
