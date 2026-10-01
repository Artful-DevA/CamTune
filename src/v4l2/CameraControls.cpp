// SPDX-License-Identifier: GPL-3.0-or-later
#include "CameraControls.h"

#include "V4l2Util.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <linux/videodev2.h>
#include <set>

namespace cam::v4l2 {

namespace {

std::string ctrlName(const char *s, size_t n)
{
    return std::string(s, strnlen(s, n));
}

bool fromExt(int fd, const v4l2_query_ext_ctrl &q, ControlInfo &c)
{
    if (q.flags & V4L2_CTRL_FLAG_DISABLED)
        return false;
    switch (q.type) {
    case V4L2_CTRL_TYPE_INTEGER:
    case V4L2_CTRL_TYPE_INTEGER64:
        c.type = ControlInfo::Type::Integer;
        break;
    case V4L2_CTRL_TYPE_BOOLEAN:
        c.type = ControlInfo::Type::Boolean;
        break;
    case V4L2_CTRL_TYPE_MENU:
        c.type = ControlInfo::Type::Menu;
        break;
    case V4L2_CTRL_TYPE_INTEGER_MENU:
        c.type = ControlInfo::Type::IntegerMenu;
        break;
    case V4L2_CTRL_TYPE_BUTTON:
        c.type = ControlInfo::Type::Button;
        break;
    default:
        return false; // strings, compound and class controls are not exposed
    }
    if (q.nr_of_dims != 0)
        return false;
    c.id = q.id;
    c.is64 = q.type == V4L2_CTRL_TYPE_INTEGER64;
    c.name = ctrlName(q.name, sizeof q.name);
    if (c.name.empty())
        c.name = "Control 0x" + std::to_string(q.id);
    c.minimum = q.minimum;
    c.maximum = q.maximum;
    c.step = int64_t(q.step);
    c.defaultValue = q.default_value;
    c.readOnly = (q.flags & (V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_GRABBED)) != 0;
    c.inactive = (q.flags & V4L2_CTRL_FLAG_INACTIVE) != 0;

    // Sanity checks: firmware occasionally reports nonsense ranges.
    if (c.type == ControlInfo::Type::Boolean) {
        c.minimum = 0;
        c.maximum = 1;
        c.step = 1;
    }
    if (c.type == ControlInfo::Type::Integer) {
        if (c.maximum < c.minimum)
            return false;
        if (c.step <= 0)
            c.step = 1;
    }
    c.defaultValue = std::clamp(c.defaultValue, c.minimum, c.maximum);

    if (c.type == ControlInfo::Type::Menu || c.type == ControlInfo::Type::IntegerMenu) {
        if (c.maximum < c.minimum || c.maximum - c.minimum > 256)
            return false;
        for (int64_t i = c.minimum; i <= c.maximum; ++i) {
            v4l2_querymenu m{};
            m.id = q.id;
            m.index = uint32_t(i);
            if (xioctl(fd, VIDIOC_QUERYMENU, &m) != 0)
                continue; // holes in menus are legal
            if (c.type == ControlInfo::Type::Menu)
                c.menu.emplace_back(i, ctrlName(reinterpret_cast<const char *>(m.name), sizeof m.name));
            else
                c.menu.emplace_back(i, std::to_string(static_cast<long long>(m.value)));
        }
        if (c.menu.empty())
            return false;
    }
    c.autoControl = autoControlFor(c.id);
    return true;
}

bool queryExt(int fd, uint32_t id, v4l2_query_ext_ctrl &q)
{
    std::memset(&q, 0, sizeof q);
    q.id = id;
    if (xioctl(fd, VIDIOC_QUERY_EXT_CTRL, &q) == 0)
        return true;
    if (errno != ENOTTY && errno != EINVAL)
        return false;
    // Older drivers: fall back to the legacy query.
    v4l2_queryctrl qc{};
    qc.id = id;
    if (xioctl(fd, VIDIOC_QUERYCTRL, &qc) != 0)
        return false;
    q.id = qc.id;
    q.type = qc.type;
    std::memcpy(q.name, qc.name, std::min(sizeof q.name, sizeof qc.name));
    q.minimum = qc.minimum;
    q.maximum = qc.maximum;
    q.step = uint64_t(std::max(1, qc.step));
    q.default_value = qc.default_value;
    q.flags = qc.flags;
    return true;
}

} // namespace

uint32_t autoControlFor(uint32_t id)
{
    switch (id) {
    case V4L2_CID_EXPOSURE_ABSOLUTE:
    case V4L2_CID_EXPOSURE:
    case V4L2_CID_IRIS_ABSOLUTE:
        return V4L2_CID_EXPOSURE_AUTO;
    case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
    case V4L2_CID_RED_BALANCE:
    case V4L2_CID_BLUE_BALANCE:
        return V4L2_CID_AUTO_WHITE_BALANCE;
    case V4L2_CID_FOCUS_ABSOLUTE:
    case V4L2_CID_FOCUS_RELATIVE:
        return V4L2_CID_FOCUS_AUTO;
    case V4L2_CID_GAIN:
        return V4L2_CID_AUTOGAIN;
    case V4L2_CID_HUE:
        return V4L2_CID_HUE_AUTO;
    default:
        return 0;
    }
}

ControlGroup controlGroup(uint32_t id)
{
    switch (id) {
    case V4L2_CID_EXPOSURE_AUTO:
    case V4L2_CID_EXPOSURE_ABSOLUTE:
    case V4L2_CID_EXPOSURE:
    case V4L2_CID_EXPOSURE_AUTO_PRIORITY:
    case V4L2_CID_AUTO_EXPOSURE_BIAS:
    case V4L2_CID_FOCUS_AUTO:
    case V4L2_CID_FOCUS_ABSOLUTE:
    case V4L2_CID_AUTOGAIN:
    case V4L2_CID_GAIN:
    case V4L2_CID_AUTO_WHITE_BALANCE:
    case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
    case V4L2_CID_BACKLIGHT_COMPENSATION:
    case V4L2_CID_POWER_LINE_FREQUENCY:
    case V4L2_CID_ZOOM_ABSOLUTE:
    case V4L2_CID_PAN_ABSOLUTE:
    case V4L2_CID_TILT_ABSOLUTE:
        return ControlGroup::Camera;
    case V4L2_CID_BRIGHTNESS:
    case V4L2_CID_CONTRAST:
    case V4L2_CID_SATURATION:
    case V4L2_CID_HUE:
    case V4L2_CID_GAMMA:
    case V4L2_CID_SHARPNESS:
        return ControlGroup::Color;
    default:
        return ControlGroup::Advanced;
    }
}

std::vector<ControlInfo> enumerateControls(int fd)
{
    std::vector<ControlInfo> out;
    std::set<uint32_t> seen;
    uint32_t id = V4L2_CTRL_FLAG_NEXT_CTRL;
    bool enumerated = false;
    for (int guard = 0; guard < 1024; ++guard) {
        v4l2_query_ext_ctrl q{};
        q.id = id;
        if (xioctl(fd, VIDIOC_QUERY_EXT_CTRL, &q) != 0) {
            if (guard == 0 && (errno == ENOTTY || errno == EINVAL)) {
                // Driver lacks NEXT_CTRL support: probe the well-known ranges.
                break;
            }
            enumerated = true;
            break;
        }
        enumerated = true;
        if (!seen.insert(q.id).second)
            break; // buggy driver looping over the same id
        ControlInfo c;
        if (fromExt(fd, q, c))
            out.push_back(std::move(c));
        id = q.id | V4L2_CTRL_FLAG_NEXT_CTRL;
    }
    if (!enumerated || out.empty()) {
        auto probe = [&](uint32_t from, uint32_t to) {
            for (uint32_t cid = from; cid < to; ++cid) {
                if (seen.count(cid))
                    continue;
                v4l2_query_ext_ctrl q{};
                if (!queryExt(fd, cid, q))
                    continue;
                seen.insert(cid);
                ControlInfo c;
                if (fromExt(fd, q, c))
                    out.push_back(std::move(c));
            }
        };
        probe(V4L2_CID_BASE, V4L2_CID_LASTP1);
        probe(V4L2_CID_CAMERA_CLASS_BASE, V4L2_CID_CAMERA_CLASS_BASE + 64);
    }
    refreshControls(fd, out);
    return out;
}

void refreshControls(int fd, std::vector<ControlInfo> &controls)
{
    for (auto &c : controls) {
        v4l2_query_ext_ctrl q{};
        if (queryExt(fd, c.id, q)) {
            c.inactive = (q.flags & V4L2_CTRL_FLAG_INACTIVE) != 0;
            c.readOnly = (q.flags & (V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_GRABBED)) != 0;
        }
        if (c.type == ControlInfo::Type::Button)
            continue;
        int64_t v;
        if (getControl(fd, c.id, v, c.is64))
            c.value = v;
    }
}

bool getControl(int fd, uint32_t id, int64_t &value, bool is64)
{
    v4l2_ext_control ctrl{};
    ctrl.id = id;
    v4l2_ext_controls ctrls{};
    ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    if (xioctl(fd, VIDIOC_G_EXT_CTRLS, &ctrls) == 0) {
        // value and value64 share storage; 32-bit controls use the low word.
        value = is64 ? ctrl.value64 : ctrl.value;
        return true;
    }
    v4l2_control c{};
    c.id = id;
    if (xioctl(fd, VIDIOC_G_CTRL, &c) != 0)
        return false;
    value = c.value;
    return true;
}

bool setControl(int fd, uint32_t id, int64_t value, std::string &error)
{
    v4l2_query_ext_ctrl q{};
    bool is64 = false;
    q.id = id;
    if (xioctl(fd, VIDIOC_QUERY_EXT_CTRL, &q) == 0)
        is64 = q.type == V4L2_CTRL_TYPE_INTEGER64;

    v4l2_ext_control ctrl{};
    ctrl.id = id;
    if (is64)
        ctrl.value64 = value;
    else
        ctrl.value = int32_t(std::clamp<int64_t>(value, INT32_MIN, INT32_MAX));
    v4l2_ext_controls ctrls{};
    ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    if (xioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) == 0)
        return true;
    int e = errno;
    if (!is64) {
        v4l2_control c{};
        c.id = id;
        c.value = int32_t(std::clamp<int64_t>(value, INT32_MIN, INT32_MAX));
        if (xioctl(fd, VIDIOC_S_CTRL, &c) == 0)
            return true;
        e = errno;
    }
    if (e == EACCES)
        error = "control is read-only or inactive (an automatic mode may be enabled)";
    else if (e == EBUSY)
        error = "control is busy";
    else
        error = errnoString(e);
    return false;
}

} // namespace cam::v4l2
