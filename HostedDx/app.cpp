#include "stdafx.h"
#include "app.hpp"
#include "effect_wrapper.hpp"
#include "graphics.hpp"
#include "fmod_helper.hpp"
#include "renderer.hpp"
#include <celsus/D3D11Descriptions.hpp>

namespace
{
	struct Lod
	{
		Lod() : _vertex_count(0), _stride(0) {}
		uint32_t	_vertex_count;
		uint32_t	_stride;
		CComPtr<ID3D11Buffer>	_vb_left;
		CComPtr<ID3D11Buffer>	_vb_right;
	};
}

IDWriteFactory *m_pDWriteFactory = NULL;
IDWriteTextFormat *m_pTextFormat = NULL;

ID2D1RenderTarget *m_pBackBufferRT = NULL;
ID2D1SolidColorBrush *m_pBackBufferTextBrush = NULL;

static const WCHAR sc_helloWorld[] = L"Hello, World!";

template<class Interface>
inline void
  SafeRelease(
  Interface **ppInterfaceToRelease
  )
{
  if (*ppInterfaceToRelease != NULL)
  {
    (*ppInterfaceToRelease)->Release();

    (*ppInterfaceToRelease) = NULL;
  }
}

App::App()
	: _loaded(false)
	, _cur_lod(0)
	, _thread_id(0xffff)
	, _thread_handle(INVALID_HANDLE_VALUE)
	, _renderer(new Renderer())
  , _cur_range(1)
  , _db_vertex_count(0)
{
}

bool App::process_command(const Command& cmd)
{
  try {
		switch (cmd._cmd) {
		case kCmdLoadMp3:
			if (!load_mp3(boost::any_cast<std::wstring>(cmd._param).c_str()))
				return false;
			break;
		case kCmdStartMp3:
			FmodHelper::instance().start();
			break;
		case kCmdIncLod:
			break;
		case kCmdDecLod:
			break;
		case kCmdIncRange:
			if (_cur_range < 65536)
				_cur_range *= 2;
			break;
		case kCmdDecRange:
			if (_cur_range > 1)
				_cur_range /= 2;
			break;
		case kCmdIncPage:
			{
				const int32_t span = 100 * _cur_range;
				FmodHelper::instance().change_pos(span);
			}
			break;
		case kCmdDecPage:
			{
				const int32_t span = 100 * _cur_range;
				FmodHelper::instance().change_pos(-span);
			}
			break;
		case kCmdSetCutoff:
			process_cutoff(boost::any_cast<float>(cmd._param));
			break;
		}
  } catch (const boost::bad_any_cast& /*e*/) {
    return false;
  }
	return true;
}

void App::report_error(const std::string& str)
{
}

void App::add_command(const Command& cmd)
{
	_command_queue.push(cmd);
}

void App::draw_text()
{
	Graphics& g = Graphics::instance();
	auto context = Graphics::instance().context();

	// draw text
	g._keyed_mutex_10->AcquireSync(0, INFINITE);
	m_pBackBufferRT->BeginDraw();
	m_pBackBufferRT->SetTransform(D2D1::Matrix3x2F::Identity());

	// Text format object will center the text in layout
	D2D1_SIZE_F rtSize = m_pBackBufferRT->GetSize();
	m_pBackBufferRT->DrawText(
		sc_helloWorld,
		ARRAYSIZE(sc_helloWorld) - 1,
		m_pTextFormat,
		D2D1::RectF(0.0f, 0.0f, rtSize.width, rtSize.height),
		m_pBackBufferTextBrush
		);

	HRESULT hr = m_pBackBufferRT->EndDraw();

	g._keyed_mutex_10->ReleaseSync(1);
	g._keyed_mutex_11->AcquireSync(1, INFINITE);

	set_vb(context, NULL, 0);
	context->VSSetShader(_fs_vs.vertex_shader(), NULL, 0);
	context->PSSetShader(_fs_ps.pixel_shader(), NULL, 0);
	context->GSSetShader(NULL, NULL, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	ID3D11SamplerState *s[] = { _sampler };
	context->PSSetSamplers(0, 1, s);
	ID3D11ShaderResourceView *r[] = { g._shared_texture_view };
	context->PSSetShaderResources(0, 1, r);
	context->OMSetDepthStencilState(_fs_depth_state, 0);
	context->Draw(4, 0);

	g._keyed_mutex_11->ReleaseSync(0);
}

void App::render()
{
	Graphics& g = Graphics::instance();
	ID3D11DeviceContext *context = g.context();

	D3DXCOLOR c(0.1f, 0.1f, 0.1f, 0);
	g.clear(c);

	if (_loaded) {

		Graphics& g = Graphics::instance();

		context->OMSetDepthStencilState(_lines_depth_state, 0);
		context->VSSetShader(_vs.vertex_shader(), NULL, 0);
		context->PSSetShader(_ps.pixel_shader(), NULL, 0);
		context->GSSetShader(NULL, NULL, 0);

		D3DXMATRIX id;
		D3DXMatrixIdentity(&id);
		_vs.set_variable("mtx", id);
		_vs.unmap_buffers();
		_vs.set_cbuffer();

		// draw background
		context->IASetInputLayout(_layout);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
		_ps.set_variable("color", D3DXCOLOR(0.2f, 0.2f, 0.2f,1));
		_ps.unmap_buffers();
		_ps.set_cbuffer();

		set_vb(context, _vb_db_lines, sizeof(D3DXVECTOR3));
		context->Draw(_db_vertex_count, 0);

		// draw foreground

		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);

		D3DXMATRIX mtx;
		D3DXMatrixIdentity(&mtx);

		int32_t ms = (int32_t)FmodHelper::instance().pos_in_ms();
		const int32_t span = 100 * _cur_range;
		const int32_t start = ms - span/2;

		_renderer->render_at_time(&_vs, &_ps, context, start, start + span, FmodHelper::instance().pos_in_ms());

		draw_text();

	}
	g.present();
}

bool App::tick()
{

	// process any commands
	Command cmd;
	while (_command_queue.try_pop(cmd)) {
		if (cmd._cmd == kCmdQuit)
			return false;
		if (!process_command(cmd))
			report_error("error running cmd");
	}

	render();

	return true;
}

float scaled_pcm_to_db(const float v)
{
  if (v == 0)
    return -FLT_MAX;
  // convert a -1..1 value to db
  return 20 * log10(abs(v));
}

float db_to_pcm(const float db)
{
  return pow(10, db / 20);
}

void cutoff_for_slice(TimeSlice *slice, const float db)
{
  slice->_vb_cutoff = NULL;
  slice->_cutoff_vertex_count = 0;

  std::vector<D3DXVECTOR3> cutoffs;
  bool prev_below = true;
  // left
  for (auto i = slice->_data_left.begin(), e = slice->_data_left.end(); i != e; ++i) {
    const D3DXVECTOR3& cur = *i;
    const float cur_value = scaled_pcm_to_db(cur.y);
    const bool cur_below = cur_value < db;
    if (prev_below && !cur_below) {
      // we just passed the cutoff, so create a mark
      cutoffs.push_back(cur - D3DXVECTOR3(0.01f, 0, 0));
      cutoffs.push_back(cur + D3DXVECTOR3(0.01f, 0, 0));
    }
    prev_below = cur_below;
  }

  prev_below = true;
  // right
  for (auto i = slice->_data_right.begin(), e = slice->_data_right.end(); i != e; ++i) {
    const D3DXVECTOR3& cur = *i;
    const float cur_value = scaled_pcm_to_db(cur.y);
    const bool cur_below = cur_value < db;
    if (prev_below && !cur_below) {
      // we just passed the cutoff, so create a mark
      cutoffs.push_back(cur - D3DXVECTOR3(0.1f, 0, 0));
      cutoffs.push_back(cur + D3DXVECTOR3(0.1f, 0, 0));
    }
    prev_below = cur_below;
  }

  if (cutoffs.empty())
    return;

  slice->_cutoff_vertex_count = cutoffs.size();
  create_static_vertex_buffer(Graphics::instance().device(), cutoffs, &slice->_vb_cutoff);

}

void App::process_cutoff(const float db)
{
  for (auto i = _renderer->_slices.begin(), e = _renderer->_slices.end(); i != e; ++i) {
    cutoff_for_slice(*i, db);
  }
}

void App::create_buffers()
{
	// create current pos bar
	std::vector<D3DXVECTOR3> v;

	// 0--1
	// |  |
	// 2--3

	const float h = 1.0f;
	const float w = 0.1f;
	v.push_back(D3DXVECTOR3(0, +h, 0));
	v.push_back(D3DXVECTOR3(w, +h, 0));
	v.push_back(D3DXVECTOR3(0, -h, 0));
	v.push_back(D3DXVECTOR3(w, -h, 0));

	create_static_vertex_buffer(Graphics::instance().device(), v, &_renderer->_vb_current_pos);
}
bool App::init()
{
	ID3D11Device *device = Graphics::instance().device();
	using namespace rt::D3D11;
	_sampler.Attach(SamplerDescription().
		Filter_(D3D11_FILTER_MIN_MAG_MIP_LINEAR).AddressU_(D3D11_TEXTURE_ADDRESS_CLAMP).AddressV_(D3D11_TEXTURE_ADDRESS_CLAMP).Create(device));
	// use default state for lines
	_lines_depth_state.Attach(DepthStencilDescription().Create(device));
	_fs_depth_state.Attach(DepthStencilDescription().
		DepthWriteMask_(D3D11_DEPTH_WRITE_MASK_ZERO).Create(device));

	create_buffers();
	return true;
}

DWORD WINAPI App::d3d_thread(void *params)
{
	App *wrapper = (App *)params;
	ThreadParams *p = &wrapper->_params;

	Graphics& g = Graphics::instance();

	if (!g.init_directx(p->hwnd, p->width, p->height))
		return NULL;

	if (!wrapper->init())
		return 1;

	while (true) {
		if (!wrapper->tick())
			break;
	}

	return 0;
}

bool App::load_mp3(const WCHAR *filename)
{
	FmodHelper& f = FmodHelper::instance();
	if (!f.load(filename))
		return false;

	int16_t *pcm = (int16_t *)f.samples();

	// split the stream into a number of 5 second chunks
	const int len_ms = 1000 * (int64_t)f.num_samples() / f.sample_rate();
	const int chunk_ms = 5000;
	const int sample_rate = f.sample_rate();
	int cur_ms = 0;
	int idx = 0;
	int stride = 128;

	// The vertices are scaled so that the unit along the x-axis is seconds (relative _start_ms), and
	// the y-axis is scaled between -1 and 1
	int ofs = 0;
	while (cur_ms < len_ms) {
		int len = std::min<int>(chunk_ms, (len_ms - cur_ms));
		int num_samples = sample_rate * len / 1000;
		int j = 0;
    TimeSlice *s = new TimeSlice();
    const int num_iterations = num_samples / stride + 1;
    s->_data_left.resize(num_iterations);
    s->_data_right.resize(num_iterations);
		for (int i = 0; i < num_samples; i += stride, ++j) {
			// scale to -1..1
			float left = pcm[(i + ofs)*2+0] / 32768.0f;
			float right = pcm[(i + ofs)*2+1] / 32768.0f;
      s->_data_left[j] = D3DXVECTOR3((float)(i / (double)sample_rate), left, 0);
      s->_data_right[j] = D3DXVECTOR3((float)(i / (double)sample_rate), right, 0);
		}
		ofs += num_samples;

		s->_vertex_count = j;
		s->_start_ms = cur_ms;
		s->_end_ms = cur_ms + len;
    create_static_vertex_buffer(Graphics::instance().device(), s->_data_left, &s->_vb_left);
    create_static_vertex_buffer(Graphics::instance().device(), s->_data_right, &s->_vb_right);

		_renderer->_slices.push_back(s);

		cur_ms += chunk_ms;
	}

	_vs.load_vertex_shader("hosteddx/stuff.fx", "vsMain");
	_ps.load_pixel_shader("hosteddx/stuff.fx", "psMain");

	_fs_vs.load_vertex_shader("hosteddx/quad.fx", "vsMain");
	_fs_ps.load_pixel_shader("hosteddx/quad.fx", "psMain");

	rt::D3D11::SamplerDescription ss;

	D3D11_INPUT_ELEMENT_DESC desc[] = { 
    CD3D11_INPUT_ELEMENT_DESC("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0),
	};
	_layout.Attach(_vs.create_input_layout(desc, ELEMS_IN_ARRAY(desc)));

  create_layout();

	_loaded = true;
	return true;
}

void App::create_layout()
{
  float db_list[] = { -0.1f, -0.5f, -1, -2, -3, -5, -10, -15, -20 };

  std::vector<D3DXVECTOR3> verts;

  for (int i = 0; i < ELEMS_IN_ARRAY(db_list); ++i) {
    const float cur = db_list[i];
    const float pcm = db_to_pcm(cur);
    verts.push_back(D3DXVECTOR3(-1, +pcm, 0));
    verts.push_back(D3DXVECTOR3(+1, +pcm, 0));
    verts.push_back(D3DXVECTOR3(-1, -pcm, 0));
    verts.push_back(D3DXVECTOR3(+1, -pcm, 0));
  }

  _db_vertex_count = verts.size();
  create_static_vertex_buffer(Graphics::instance().device(), verts, &_vb_db_lines);
}

App::~App()
{
	SAFE_DELETE(_renderer);
	if (_thread_handle != INVALID_HANDLE_VALUE) {
		_command_queue.push(Command(kCmdQuit));
		WaitForSingleObject(_thread_handle, INFINITE);
	}
	Graphics::instance().close();
	_layout = NULL;
	_instance = NULL;
}

void App::close()
{
	delete this;
}

void App::run(HWND hwnd, int width, int height)
{
	_params.hwnd = hwnd;
	_params.width = width;
	_params.height = height;
	_thread_handle = CreateThread(0, 0, d3d_thread, this, 0, &_thread_id);
}

bool App::is_created() 
{ 
  return _instance != NULL; 
}

App& App::instance()
{
  if (!_instance)
    _instance = new App();
  return *_instance;
}


App *App::_instance = NULL;
