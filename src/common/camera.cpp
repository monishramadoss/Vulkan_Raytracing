#include "camera.h"

static const vec3 sCameraUp(0.0f, 1.0f, 0.0f);

Camera::Camera()
	: _FovY(65.0f)
	, _NearZ(1.0f)
	, _FarZ(1000.0f)
	, _Position(0.0f, 10.0f, 10.0f)
	, _Direction(0.0f, 0.0f, 1.0f)
{
}

Camera::~Camera() {
}

void Camera::SetViewport(const Recti& viewport) {
	_Viewport = viewport;
	MakeProjection();
}

void Camera::SetFovY(const float fovy) {
	_FovY = fovy;
	MakeProjection();
}

void Camera::SetViewPlanes(const float nearZ, const float farZ) {
	_NearZ = nearZ;
	_FarZ = farZ;
	MakeProjection();
}

void Camera::SetPosition(const vec3& pos) {
	_Position = pos;
	MakeTransform();
}

void Camera::LookAt(const vec3& pos, const vec3& target) {
	_Position = pos;
	_Direction = normalize(target - pos);

	MakeTransform();
}

void Camera::Move(const float side, const float direction) {
	vec3 cameraSide = normalize(cross(_Direction, sCameraUp));

	_Position += cameraSide * side;
	_Position += _Direction * direction;

	MakeTransform();
}

void Camera::Rotate(const float angleX, const float angleY) {
	vec3 side = cross(_Direction, sCameraUp);
	quat pitchQ = QAngleAxis(Deg2Rad(angleY), side);
	quat headingQ = QAngleAxis(Deg2Rad(angleX), sCameraUp);
	quat temp = normalize(pitchQ * headingQ);
	_Direction = normalize(QRotate(temp, _Direction));

	MakeTransform();
}

float Camera::GetNearPlane() const {
	return _NearZ;
}

float Camera::GetFarPlane() const {
	return _FarZ;
}

float Camera::GetFovY() const {
	return _FovY;
}

const mat4& Camera::GetProjection() const {
	return _Projection;
}

const mat4& Camera::GetTransform() const {
	return _Transform;
}

const vec3& Camera::GetPosition() const {
	return _Position;
}

const vec3& Camera::GetDirection() const {
	return _Direction;
}

const vec3 Camera::GetUp() const {
	return vec3(_Transform[0][1], _Transform[1][1], _Transform[2][1]);
}

const vec3 Camera::GetSide() const {
	return vec3(_Transform[0][0], _Transform[1][0], _Transform[2][0]);
}

void Camera::MakeProjection() {
	const float aspect = static_cast<float>(_Viewport.right - _Viewport.left) / static_cast<float>(_Viewport.bottom - _Viewport.top);
	_Projection = MatProjection(Deg2Rad(_FovY), aspect, _NearZ, _FarZ);
}

void Camera::MakeTransform() {
	_Transform = MatLookAt(_Position, _Position + _Direction, sCameraUp);
}
