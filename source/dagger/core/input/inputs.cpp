#include "inputs.h"
#include "core/engine.h"

#include <GLFW/glfw3.h>

using namespace dagger;

void InputSystem::OnKeyboardEvent(KeyboardEvent input_)
{
	auto key = (UInt64)input_.key;

	if (input_.action == DaggerInputState::Pressed)
	{
		m_InputState.keys[key] = true;
		m_InputState.moments[key] = Engine::CurrentTime();
		m_InputState.bitmap.set(key);
	}
	else if (input_.action == DaggerInputState::Released)
	{
		m_InputState.releasedLastFrame.emplace(key);
		m_InputState.keys[key] = false;
		m_InputState.moments.erase(key);
		// NOTE: not a bug! bitmap not reset here but after next update via `justReleased`!
	}
}

void InputSystem::OnMouseEvent(MouseEvent input_)
{
	UInt32 button = (UInt64)input_.button;

	if (input_.action == DaggerInputState::Pressed)
	{
		m_InputState.mouse[button - MouseStart] = true;
		m_InputState.moments[button] = std::chrono::steady_clock::now();
		m_InputState.bitmap.set(button);
	}
	else if (input_.action == DaggerInputState::Released)
	{
		m_InputState.releasedLastFrame.emplace(button);
		m_InputState.mouse[button - MouseStart] = false;
		m_InputState.moments.erase(button);
		// NOTE: not a bug! bitmap not reset here but after next update via `justReleased`!
	}
}

void InputSystem::SpinUp() 
{
	Engine::Dispatcher().sink<AssetLoadRequest<InputContext>>().connect<&InputSystem::OnAssetLoadRequest>(this);
	Engine::Dispatcher().sink<KeyboardEvent>().connect<&InputSystem::OnKeyboardEvent>(this);
	Engine::Dispatcher().sink<MouseEvent>().connect<&InputSystem::OnMouseEvent>(this);

	Engine::Res<InputState>()["input"] = &m_InputState;

	LoadDefaultAssets();
};

void InputSystem::LoadDefaultAssets()
{
	for (auto& entry : Files::recursive_directory_iterator("input-contexts"))
	{
		auto path = entry.path().string();
		if (entry.is_regular_file() && entry.path().extension() == ".json")
		{
			Engine::Dispatcher().trigger<AssetLoadRequest<InputContext>>(AssetLoadRequest<InputContext>{ path });
		}
	}
}

void InputSystem::LoadInputAction(InputCommand& command_, JSON::json& input_)
{
	InputAction action;
	assert(input_.contains("trigger"));
	assert(s_InputValues.contains(input_["trigger"]));
	action.trigger = s_InputValues[input_["trigger"]];

	if (input_.contains("duration"))
	{
		action.duration = input_["duration"];
	}
	else
		action.duration = 0;

	if (input_.contains("event"))
	{
		String eventHandler = input_["event"];
		if (eventHandler == "Pressed")
			action.event = DaggerInputState::Pressed;
		else if (eventHandler == "Held")
			action.event = DaggerInputState::Held;
		else if (eventHandler == "Released")
			action.event = DaggerInputState::Released;
	}
	else
		action.event = DaggerInputState::Held;

	if (input_.contains("value"))
	{
		action.value = input_["value"];
	}
	else
		action.value = 1;

	command_.actions.push_back(std::move(action));
}

void InputSystem::OnAssetLoadRequest(AssetLoadRequest<InputContext> request_)
{
	FilePath path(request_.path);
	Logger::info("Loading '{}'", request_.path);

	if (!Files::exists(path))
	{
		Engine::Dispatcher().trigger<Error>(Error{ fmt::format("Couldn't load input context from {}.", request_.path) });
		return;
	}

	FileInputStream handle;
	auto absolutePath = Files::absolute(path);
	handle.open(absolutePath);

	if (!handle.is_open())
	{
		Engine::Dispatcher().trigger<Error>(Error{ fmt::format("Couldn't open input context file '{}' for reading.", absolutePath.string()) });
		return;
	}

	JSON::json json;
	handle >> json;

	InputContext* context = new InputContext();
	assert(json.contains("context-name"));
	assert(json.contains("commands"));

	context->name = json["context-name"];
	for(auto& cmd: json["commands"])
	{
		InputCommand command;
		assert(cmd.contains("command-name"));
		command.name = cmd["command-name"];

		if (cmd.contains("actions"))
		{
			for (auto& action : cmd["actions"])
			{
				LoadInputAction(command, action);
			}
		}
		else
		{
			LoadInputAction(command, cmd);
		}

		for (auto& action : command.actions)
		{
			context->bitmap.set(action.trigger, true);
		}
		
		context->commands.push_back(std::move(command));
	}

	auto& library = Engine::Res<InputContext>();
	if (library.contains(context->name))
	{
		delete library[context->name];
	}

	library[context->name] = context;
	Logger::info("Input context '{}' loaded!", context->name);
}

void InputSystem::Run()
{
	Engine::Registry().view<InputReceiver>().each([&](InputReceiver& receiver_)
		{
			static Set<String> updatedCommands{};

			auto& bitmap = m_InputState.bitmap;
			auto& library = Engine::Res<InputContext>();
			for (auto& name : receiver_.contexts)
			{
				assert(library.contains(name));
				const auto& context = library[name];
				const auto& collision = (bitmap & context->bitmap);
				if (collision.any())
				{
					for (auto& command : context->commands)
					{
						String fullName = fmt::format("{}:{}", name, command.name);
						for (auto& action : command.actions)
						{
							if (action.event == DaggerInputState::Released)
							{
								if (m_InputState.releasedLastFrame.contains(action.trigger))
								{
									receiver_.values[fullName] = action.value;
									updatedCommands.emplace(fullName);
								}
							}
							else
							{
								if (m_InputState.releasedLastFrame.contains(action.trigger))
								{
									receiver_.values[fullName] = 0;
								}

								Bool toFire = false;
								const Bool toConsume = action.event == DaggerInputState::Pressed;

								if (action.duration == 0)
								{
									// mouse
									if (action.trigger >= MouseStart && action.trigger <= (MouseStart + 10))
									{
										if (input::IsInputDown((DaggerMouse)(action.trigger)))
										{
											toFire = true;
											if (toConsume) 
												m_InputState.releasedLastFrame.emplace(action.trigger);
										}
									}
									else
									{
										if (input::IsInputDown((DaggerKeyboard)(action.trigger)))
										{
											toFire = true;
											if (toConsume) m_InputState.releasedLastFrame.emplace(action.trigger);
										}
									}
								}
								else
								{
									// mouse
									if (action.trigger >= MouseStart && action.trigger <= (MouseStart + 10))
									{
										if (input::GetInputDuration((DaggerMouse)(action.trigger)) >= action.duration)
										{
											toFire = true;
											m_InputState.releasedLastFrame.emplace(action.trigger);
										}
									}
									else
									{
										if (input::GetInputDuration((DaggerKeyboard)(action.trigger)) >= action.duration)
										{
											toFire = true;
											m_InputState.releasedLastFrame.emplace(action.trigger);
										}
									}
								}

								if (toFire)
								{
									receiver_.values[fullName] = action.value;
									updatedCommands.emplace(fullName);
								}
							}
						}
					}
				}

				for (auto& [key, value] : receiver_.values)
				{
					if (updatedCommands.contains(key)) continue;
					receiver_.values[key] = 0;
				}

				updatedCommands.clear();
			}
		});

	if (!m_InputState.releasedLastFrame.empty())
	{
		for (auto& input : m_InputState.releasedLastFrame)
		{
			m_InputState.bitmap.reset(input);
		}

		m_InputState.releasedLastFrame.clear();
	}
};

void InputSystem::WindDown() 
{
	Engine::Dispatcher().sink<AssetLoadRequest<InputContext>>().disconnect<&InputSystem::OnAssetLoadRequest>(this);
	Engine::Dispatcher().sink<KeyboardEvent>().disconnect<&InputSystem::OnKeyboardEvent>(this);
	Engine::Dispatcher().sink<MouseEvent>().disconnect<&InputSystem::OnMouseEvent>(this);
};

inline Bool dagger::input::IsInputDown(DaggerKeyboard key_)
{
	const auto* state = Engine::Res<InputState>()["input"];
	return state->keys[(UInt32)key_];
}

inline Bool dagger::input::IsInputDown(DaggerMouse button_)
{
	const auto* state = Engine::Res<InputState>()["input"];
	return state->mouse[(UInt32)button_ - MouseStart];
}

inline UInt32 dagger::input::GetInputDuration(DaggerKeyboard key_)
{
	const auto* state = Engine::Res<InputState>()["input"];

	UInt32 value = (UInt32)key_;
	if (!state->moments.contains(value)) return 0;

	return DurationToMilliseconds(Engine::CurrentTime() - state->moments.at(value)).count();
}

inline UInt32 dagger::input::GetInputDuration(DaggerMouse mouse_)
{
	const auto* state = Engine::Res<InputState>()["input"];

	UInt32 value = (UInt32)mouse_;
	return DurationToMilliseconds(Engine::CurrentTime() - state->moments.at(value)).count();
}

inline void dagger::input::ConsumeInput(DaggerKeyboard key_)
{
	auto* state = Engine::Res<InputState>()["input"];

	UInt32 value = (UInt32)key_;
	state->keys[value] = false;
	state->moments.erase(value);
	state->bitmap.reset(value);
}

inline void dagger::input::ConsumeInput(DaggerMouse button_)
{
	auto* state = Engine::Res<InputState>()["input"];

	UInt32 value = (UInt32)button_;
	state->mouse[value - MouseStart] = false;
	state->moments.erase(value);
	state->bitmap.reset(value);
}
