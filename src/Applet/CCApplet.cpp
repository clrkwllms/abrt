/*
    Copyright (C) 2009  Jiri Moskovcak (jmoskovc@redhat.com)
    Copyright (C) 2009  RedHat inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    */

#include "abrtlib.h"
#include "CCApplet.h"
#include <iostream>
#include <cstdarg>
#include <sstream>
#include <cstdio>
#include <unistd.h>



#if HAVE_CONFIG_H
    #include <config.h>
#endif

#if ENABLE_NLS
    #include <libintl.h>
    #define _(S) gettext(S)
#else
    #define _(S) (S)
#endif



static const char *DBUS_SERVICE_NAME = "org.freedesktop.DBus";
static const char *DBUS_SERVICE_PATH = "/org/freedesktop/DBus";
const gchar *CApplet::menu_xml =
        "<?xml version=\"1.0\"?>\
<interface>\
  <requires lib=\"gtk+\" version=\"2.16\"/>\
  <!-- interface-naming-policy project-wide -->\
  <object class=\"GtkMenu\" id=\"popup_menu\">\
    <property name=\"visible\">True</property>\
    <child>\
      <object class=\"GtkMenuItem\" id=\"miHide\">\
        <property name=\"visible\">True</property>\
        <property name=\"label\" translatable=\"yes\">Hide</property>\
      </object>\
    </child>\
    <child>\
      <object class=\"GtkImageMenuItem\" id=\"miQuit\">\
        <property name=\"label\">gtk-quit</property>\
        <property name=\"visible\">True</property>\
        <property name=\"use_underline\">True</property>\
        <property name=\"use_stock\">True</property>\
        <property name=\"always_show_image\">True</property>\
      </object>\
    </child>\
    <child>\
      <object class=\"GtkSeparatorMenuItem\" id=\"miSep1\">\
        <property name=\"visible\">True</property>\
      </object>\
    </child>\
    <child>\
      <object class=\"GtkImageMenuItem\" id=\"miAbout\">\
        <property name=\"label\">gtk-about</property>\
        <property name=\"visible\">True</property>\
        <property name=\"use_underline\">True</property>\
        <property name=\"use_stock\">True</property>\
        <property name=\"always_show_image\">True</property>\
      </object>\
    </child>\
  </object>\
  <object class=\"GtkAboutDialog\" id=\"aboutdialog\">\
    <property name=\"border_width\">5</property>\
    <property name=\"type_hint\">normal</property>\
    <property name=\"has_separator\">False</property>\
    <property name=\"program_name\">Automatic Bug Reporting Tool</property>\
    <property name=\"copyright\" translatable=\"yes\">Copyright &#xA9; 2009 Red Hat, Inc</property>\
    <property name=\"website\">https://fedorahosted.org/abrt/</property>\
    <property name=\"website_label\" translatable=\"yes\">Website</property>\
    <property name=\"license\" translatable=\"yes\">This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.\n\
\n\
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.\n\
\n\
You should have received a copy of the GNU General Public License along with this program.  If not, see &lt;http://www.gnu.org/licenses/&gt;.</property>\
    <property name=\"authors\">Jiri Moskovcak  &lt;jmoskovc@redhat.com&gt;</property>\
    <property name=\"wrap_license\">True</property>\
    <child internal-child=\"vbox\">\
      <object class=\"GtkVBox\" id=\"dialog-vbox1\">\
        <property name=\"visible\">True</property>\
        <property name=\"orientation\">vertical</property>\
        <property name=\"spacing\">2</property>\
        <child>\
          <placeholder/>\
        </child>\
        <child internal-child=\"action_area\">\
          <object class=\"GtkHButtonBox\" id=\"dialog-action_area1\">\
            <property name=\"visible\">True</property>\
            <property name=\"layout_style\">end</property>\
          </object>\
          <packing>\
            <property name=\"expand\">False</property>\
            <property name=\"pack_type\">end</property>\
            <property name=\"position\">0</property>\
          </packing>\
        </child>\
      </object>\
    </child>\
  </object>\
</interface>";

CApplet::CApplet(DBus::Connection &system, DBus::Connection &session, const char *path, const char *name)
: DBus::ObjectProxy(system, path, name)
{
    m_pSessionDBus = &session;
    m_pDaemonWatcher = new DaemonWatcher(system, DBUS_SERVICE_PATH, DBUS_SERVICE_NAME);
    m_pDaemonWatcher->ConnectStateChangeHandler(DaemonStateChange_cb, this);
    m_pStatusIcon = gtk_status_icon_new_from_stock(GTK_STOCK_DIALOG_WARNING);
    m_bDaemonRunning = true;
    notify_init("ABRT");
    m_pNotification = notify_notification_new_with_status_icon("Warning", NULL, NULL, m_pStatusIcon);
    notify_notification_set_urgency(m_pNotification, NOTIFY_URGENCY_CRITICAL);
    notify_notification_set_timeout(m_pNotification, 5000);
    gtk_status_icon_set_visible(m_pStatusIcon, FALSE);
    g_signal_connect(G_OBJECT(m_pStatusIcon), "activate", GTK_SIGNAL_FUNC(CApplet::OnAppletActivate_CB), this);
    g_signal_connect(G_OBJECT(m_pStatusIcon), "popup_menu", GTK_SIGNAL_FUNC(CApplet::OnMenuPopup_cb), this);
    SetIconTooltip(_("Pending events: %i"), m_mapEvents.size());
    m_pBuilder = gtk_builder_new();
    if (gtk_builder_add_from_string(m_pBuilder, menu_xml, strlen(menu_xml), NULL))
    //if (gtk_builder_add_from_file(m_pBuilder, "popup.GtkBuilder", NULL))
    {
        m_pMenu = gtk_builder_get_object(m_pBuilder, "popup_menu");
        //gtk_menu_attach_to_widget(GTK_MENU(m_pMenu), GTK_WIDGET(m_pStatusIcon), NULL);
        m_pmiHide = gtk_builder_get_object(m_pBuilder, "miHide");
        if (m_pmiHide != NULL)
        {
            g_signal_connect(m_pmiHide,"activate", G_CALLBACK(CApplet::onHide_cb), this);
        }
        m_pmiQuit = gtk_builder_get_object(m_pBuilder, "miQuit");
        if (m_pmiQuit != NULL)
        {
            g_signal_connect(m_pmiQuit,"activate",G_CALLBACK(gtk_main_quit),NULL);
        }
        m_pAboutDialog = gtk_builder_get_object(m_pBuilder, "aboutdialog");
        m_pmiAbout = gtk_builder_get_object(m_pBuilder, "miAbout");
        {
            g_signal_connect(m_pmiAbout,"activate",G_CALLBACK(CApplet::onAbout_cb),m_pAboutDialog);
        }
    }
    else
    {
        fprintf(stderr,_("Can't create menu from the description, popup won't be available!\n"));
    }
}

CApplet::~CApplet()
{
    delete m_pDaemonWatcher;
}

/* dbus related */
void CApplet::Crash(const std::string& progname, const std::string& uid  )
{
    if (m_pSessionDBus->has_name("com.redhat.abrt.gui"))
    {
        return;
    }
    else
    {
        if (m_pCrashHandler)
        {
            std::istringstream input_string(uid);
            uid_t num;
            input_string >> num;

            if (num == getuid())
                m_pCrashHandler(progname.c_str());
        }
        else
        {
            std::cout << _("This is default handler, you should register your own with ConnectCrashHandler") << std::endl;
            std::cout.flush();
        }
    }
}

void CApplet::DaemonStateChange_cb(bool running, void* data)
{
    CApplet *applet = (CApplet *)data;
    if (!running)
    {
        applet->Disable(_("ABRT service is not running"));
    }
    else
    {
        applet->Enable(_("ABRT service has been started"));
    }
}

void CApplet::ConnectCrashHandler(void (*pCrashHandler)(const char *progname))
{
    m_pCrashHandler = pCrashHandler;
}

void CApplet::SetIconTooltip(const char *format, ...)
{
    va_list args;
    int n;
    char *buf;

    va_start(args, format);
    buf = NULL;
    n = vasprintf(&buf, format, args);
    va_end(args);
    if (n >= 0 && buf)
    {
        gtk_status_icon_set_tooltip_text(m_pStatusIcon, buf);
        free(buf);
    }
    else
    {
        gtk_status_icon_set_tooltip_text(m_pStatusIcon, _("Out of memory"));
    }
}

void CApplet::CrashNotify(const char *format, ...)
{
    va_list args;
    char *buf;
    int n;
    GError *err = NULL;

    va_start(args, format);
    buf = NULL;
    n = vasprintf(&buf, format, args);
    va_end(args);

    notify_notification_update(m_pNotification, _("Warning"), buf, NULL);
    if (gtk_status_icon_is_embedded (m_pStatusIcon))
        notify_notification_show(m_pNotification, &err);
    if (err != NULL)
        g_print(err->message);
}

void CApplet::OnAppletActivate_CB(GtkStatusIcon *status_icon,gpointer user_data)
{
    CApplet *applet = (CApplet *)user_data;
    if (applet->m_bDaemonRunning)
    {
        pid_t pid = vfork();
        if (pid < 0)
            std::cerr << "vfork failed\n";
        if (pid == 0)
        { /* child */
            signal(SIGCHLD, SIG_DFL); /* undo SIG_IGN in abrt-applet */
            execl(BIN_DIR"/abrt-gui", "abrt-gui", (char*) NULL);
            /* Did not find abrt-gui in installation directory. Oh well */
            /* Trying to find it in PATH */
            execlp("abrt-gui", "abrt-gui", (char*) NULL);
            std::cerr << "can't exec abrt-gui\n";
            exit(1);
        }
        gtk_status_icon_set_visible(applet->m_pStatusIcon, false);
    }
}

void CApplet::OnMenuPopup_cb(GtkStatusIcon *status_icon,
                            guint          button,
                            guint          activate_time,
                            gpointer       user_data)
{
    if (((CApplet *)user_data)->m_pMenu != NULL)
    {
        gtk_menu_popup(GTK_MENU(((CApplet *)user_data)->m_pMenu),NULL,NULL,gtk_status_icon_position_menu,status_icon,button,activate_time);
    }
}

void CApplet::ShowIcon()
{
    gtk_status_icon_set_visible(m_pStatusIcon, true);
    //Active wait for icon to be REALLY visible in status area
    //while(!gtk_status_icon_is_embedded (m_pStatusIcon));
}

void CApplet::onHide_cb(GtkMenuItem *menuitem, gpointer applet)
{
    ((CApplet*)applet)->HideIcon();
}

void CApplet::onAbout_cb(GtkMenuItem *menuitem, gpointer dialog)
{
    if (dialog)
        gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_hide(GTK_WIDGET(dialog));
}

void CApplet::HideIcon()
{
    gtk_status_icon_set_visible(m_pStatusIcon, false);
}

void CApplet::Disable(const char *reason)
{
    /*
        FIXME: once we have our icon
    */
    m_bDaemonRunning = false;
    GdkPixbuf *gray_scaled;
    GdkPixbuf *pixbuf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), GTK_STOCK_DIALOG_WARNING, 24, GTK_ICON_LOOKUP_USE_BUILTIN, NULL);
    gray_scaled = gdk_pixbuf_copy(pixbuf);
    if (pixbuf)
    {
        gdk_pixbuf_saturate_and_pixelate(pixbuf, gray_scaled, 0.0, NULL);
        gtk_status_icon_set_from_pixbuf(m_pStatusIcon, gray_scaled);
    }
    else
        std::cerr << "Cannot load icon!" << std::endl;
    SetIconTooltip(reason);
    ShowIcon();
}

void CApplet::Enable(const char *reason)
{
    /* restore the original icon */
    m_bDaemonRunning = true;
    SetIconTooltip(reason);
    gtk_status_icon_set_from_stock(m_pStatusIcon, GTK_STOCK_DIALOG_WARNING);
    ShowIcon();
}

int CApplet::AddEvent(int pUUID, const std::string& pProgname)
{
    m_mapEvents[pUUID] = "pProgname";
    SetIconTooltip(_("Pending events: %i"), m_mapEvents.size());
    return 0;
}

int CApplet::RemoveEvent(int pUUID)
{
     m_mapEvents.erase(pUUID);
     return 0;
}
void CApplet::BlinkIcon(bool pBlink)
{
    gtk_status_icon_set_blinking(m_pStatusIcon, pBlink);
}
