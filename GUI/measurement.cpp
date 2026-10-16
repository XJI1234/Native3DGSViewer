#include "pch.h"
#include "MainWindow.xaml.h"
#include "app_log.h"
#include "render-core/picking.h"
#include <shobjidl.h>
#include <iomanip>
#include <sstream>

namespace winrt::Native3DGSViewer::implementation
{
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
std::wstring MainWindow::measurement_error(gs::engine::MeasurementError error)
{
    using E = gs::engine::MeasurementError;
    switch (error)
    {
    case E::InvalidJson:
        return L"无法读取有效的比例尺 JSON，请检查文件内容";
    case E::ResourceLimit:
        return L"文件或拾取候选超出上限，请更换文件或选择其他位置";
    case E::UnsupportedSchema:
        return L"不支持此比例尺版本或坐标约定";
    case E::UnavailableCalibration:
        return L"此标定不可用，请导入可用的比例尺";
    case E::InvalidTransform:
        return L"比例尺或矩阵无效，无法计算距离";
    case E::IoFailure:
        return L"无法读取比例尺文件，请重新选择";
    case E::Cancelled:
        return L"操作已取消";
    case E::NoScene:
        return L"请等待模型就绪后测距";
    case E::Busy:
        return L"请等待当前操作完成，或点击重新测量";
    case E::StaleRequest:
        return L"视图已变化，请重新选择此点";
    case E::Timeout:
        return L"定位超时，请重新选点";
    case E::NoCalibration:
        return L"请先导入比例尺";
    default:
        return L"图形设备暂不可用，请等待恢复";
    }
}

winrt::fire_and_forget MainWindow::pick_metric()
{
    if (!engine_ || !measurement_mode_ || metric_picker_open_)
        co_return;
    auto lifetime = get_strong();
    const auto scene = engine_->snapshot().active_request;
    metric_picker_open_ = true;
    keys_.fill(false);
    dragging_ = fly_capture_ = measurement_click_ = false;
    scene_panel_.ReleasePointerCaptures();
    try
    {
        Windows::Storage::Pickers::FileOpenPicker picker;
        picker.FileTypeFilter().Append(L".json");
        auto initialize = picker.as<IInitializeWithWindow>();
        winrt::check_hresult(initialize->Initialize(hwnd_));
        auto file = co_await picker.PickSingleFileAsync();
        if (!closing_ && file && measurement_mode_ && engine_ &&
            engine_->snapshot().active_request == scene)
            if (auto error = engine_->import_metric(std::filesystem::path{file.Path().c_str()}))
                measurement_hint_.Text(measurement_error(*error));
    }
    catch (...)
    {
        viewer::log::write("error", "metric_picker_failed");
        if (!closing_)
            set_status(L"无法打开比例尺选择器");
    }
    metric_picker_open_ = false;
}
void MainWindow::set_measurement_mode(bool enabled)
{
    if (!engine_)
        return;
    if (auto error = engine_->set_measurement_enabled(enabled))
    {
        set_status(measurement_error(*error));
        updating_measure_button_ = true;
        measure_button_.IsChecked(measurement_mode_);
        updating_measure_button_ = false;
        return;
    }
    if (measurement_mode_ != enabled)
    {
        if (enabled)
            restore_fly_ = fly_mode_;
        measurement_mode_ = enabled;
        if (enabled)
            orbit_button_.IsChecked(true);
        else if (restore_fly_)
            fly_button_.IsChecked(true);
        fly_capture_ = dragging_ = measurement_click_ = false;
        keys_.fill(false);
        scene_panel_.ReleasePointerCaptures();
    }
    orbit_button_.IsEnabled(!enabled);
    fly_button_.IsEnabled(!enabled);
    updating_measure_button_ = true;
    measure_button_.IsChecked(enabled);
    updating_measure_button_ = false;
    measurement_panel_.Visibility(enabled ? Visibility::Visible : Visibility::Collapsed);
    update_measurement(engine_->snapshot());
}
void MainWindow::update_measurement(const gs::engine::Snapshot &state)
{
    const auto &m = state.measurement;
    if (measurement_mode_ && !m.enabled)
    {
        set_measurement_mode(false);
        return;
    }
    measure_button_.IsEnabled(state.phase == gs::engine::Phase::Ready || measurement_mode_);
    measurement_overlay_.Visibility(m.enabled && state.phase == gs::engine::Phase::Ready
                                        ? Visibility::Visible
                                        : Visibility::Collapsed);
    if (!m.enabled)
        return;
    import_metric_button_.IsEnabled(state.phase == gs::engine::Phase::Ready && !m.importing &&
                                    !metric_picker_open_);
    restart_measurement_button_.IsEnabled(m.calibration.has_value() || m.importing);
    std::wstring hint;
    if (state.phase != gs::engine::Phase::Ready)
        hint = L"模型暂不可测量，请等待就绪";
    else if (m.importing)
        hint = L"正在读取并检查比例尺…可退出取消";
    else if (m.picking)
        hint = L"正在定位表面…";
    else if (m.error)
        hint = measurement_error(*m.error) + (m.calibration ? L"（保留当前比例尺）" : L"");
    else if (!m.calibration)
        hint = L"请导入模型生成时输出的 .metric.json";
    else if (m.missed)
        hint = L"此处没有可测量的模型表面，请重新选择";
    else if (m.b)
        hint = L"测量完成，点击“重新测量”开始下一条";
    else
        hint = m.a ? L"请选择终点 B" : L"请选择起点 A";
    measurement_hint_.Text(hint);
    std::wstring quality = L"用户指定比例尺，未校验模型对应关系";
    if (m.calibration)
    {
        std::wostringstream text;
        text << L"\n比例尺：" << std::setprecision(8) << m.calibration->meters_per_model_unit
             << L" m/模型单位\n";
        text << (m.calibration->independently_validated ? L"已实测验证（导入文件声明）"
                                                        : L"GPS 估算尺度，未实测验证");
        quality += text.str();
    }
    metric_quality_text_.Text(quality);
    std::wostringstream distance;
    if (m.distance_m)
        distance << L"距离：" << std::fixed << std::setprecision(3) << *m.distance_m << L" m";
    else
        distance << L"距离：—";
    measurement_result_.Text(distance.str());
    const auto size = viewport();
    if (!size.physical_width || !size.physical_height)
        return;
    const double scale = scene_panel_.XamlRoot().RasterizationScale();
    auto clip = Microsoft::UI::Xaml::Media::RectangleGeometry{};
    clip.Rect({0, 0, float(scene_panel_.ActualWidth()), float(scene_panel_.ActualHeight())});
    measurement_overlay_.Clip(clip);
    auto show = [&](const auto &point, TextBlock marker) -> std::optional<gs::render::ScreenPoint> {
        marker.Visibility(Visibility::Collapsed);
        if (!point)
            return {};
        auto projected = gs::render::project_world(point->position_rub, state.camera, size);
        if (!projected)
            return {};
        if (flip_y_)
            projected->x_px = size.physical_width - projected->x_px;
        if (projected->x_px >= 0 && projected->x_px < size.physical_width && projected->y_px >= 0 &&
            projected->y_px < size.physical_height)
        {
            Canvas::SetLeft(marker, projected->x_px / scale - 5);
            Canvas::SetTop(marker, projected->y_px / scale - 14);
            marker.Visibility(Visibility::Visible);
        }
        return projected;
    };
    show(m.a, marker_a_);
    show(m.b, marker_b_);
    auto line = m.a && m.b ? gs::render::project_segment(m.a->position_rub, m.b->position_rub,
                                                         state.camera, size)
                           : std::nullopt;
    measurement_line_.Visibility(line ? Visibility::Visible : Visibility::Collapsed);
    if (line)
    {
        if (flip_y_)
            for (auto &point : *line)
                point.x_px = size.physical_width - point.x_px;
        measurement_line_.X1((*line)[0].x_px / scale);
        measurement_line_.Y1((*line)[0].y_px / scale);
        measurement_line_.X2((*line)[1].x_px / scale);
        measurement_line_.Y2((*line)[1].y_px / scale);
    }
}

} // namespace winrt::Native3DGSViewer::implementation
