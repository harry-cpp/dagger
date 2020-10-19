#include "sprite.h"
#include "texture.h"
#include "textures.h"

void Sprite::Use(String textureName_)
{
	Texture* texture = TextureSystem::Get(textureName_);

	// we copy this over so that this info can be copied into GPU-space directly
	m_Image = texture->Index();
	m_Ratio = texture->Ratio();
}