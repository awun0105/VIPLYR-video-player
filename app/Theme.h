#pragma once
#include <QString>

// Style sheets shared by the widgets.
namespace Theme {
    const QString Accent = "#3D9BFF";

    const QString WindowBg = "background-color: #0c0c0e; color: white; font-family: sans-serif;";
    const QString PanelBg = "background-color: #17171a; color: #9a9aa2; border: none; outline: none;";
    const QString PlaylistList =
        "QListWidget { background-color: #17171a; color: #9a9aa2; border: none; outline: none; } "
        "QListWidget::item { padding: 6px; border-radius: 6px; margin: 2px 0; } "
        "QListWidget::item:hover { background-color: #24242a; color: #d0d0d6; } "
        "QListWidget::item:selected { background-color: #1f3a5c; color: white; }";
    const QString Button =
        "QPushButton { background-color: #2a2a31; border-radius: 6px; padding: 7px 12px; "
        "font-weight: bold; border: none; color: white; } "
        "QPushButton:hover { background-color: #3a3a44; } "
        "QPushButton::menu-indicator { width: 0; }";
    const QString SearchBox =
        "QLineEdit { background-color: #222228; color: #d0d0d6; border: 1px solid #2e2e36; "
        "border-radius: 6px; padding: 6px 8px; } QLineEdit:focus { border-color: #3D9BFF; }";
    const QString Menu =
        "QMenu { background-color: #24242a; color: white; border: 1px solid #34343c; padding: 4px; } "
        "QMenu::item { padding: 6px 22px; border-radius: 4px; } "
        "QMenu::item:selected { background-color: #3D9BFF; } "
        "QMenu::item:disabled { color: #70707a; } "
        "QMenu::separator { height: 1px; background: #34343c; margin: 4px 8px; }";

    // Buttons that sit on top of the video: icon only, no background until hovered.
    const QString OverlayButton =
        "QToolButton { background: transparent; border: none; border-radius: 6px; padding: 6px; } "
        "QToolButton:hover { background-color: rgba(255, 255, 255, 40); } "
        "QToolButton:pressed { background-color: rgba(255, 255, 255, 70); } "
        "QToolButton::menu-indicator { image: none; }";

    const QString SliderGroove = "background: rgba(255, 255, 255, 60); height: 4px; border-radius: 2px;";
    const QString SliderHandle =
        "background: white; width: 12px; margin-top: -4px; margin-bottom: -4px; border-radius: 6px;";
    const QString SeekColor = "background: #3D9BFF; border-radius: 2px;";
    const QString VolColor = "background: white; border-radius: 2px;";
}
