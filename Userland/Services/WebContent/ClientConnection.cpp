/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <AK/Badge.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/SystemTheme.h>
#include <WebContent/ClientConnection.h>
#include <WebContent/PageHost.h>
#include <WebContent/WebContentClientEndpoint.h>

namespace WebContent {

static HashMap<int, RefPtr<ClientConnection>> s_connections;

ClientConnection::ClientConnection(NonnullRefPtr<Core::LocalSocket> socket, int client_id)
    : IPC::ClientConnection<WebContentClientEndpoint, WebContentServerEndpoint>(*this, move(socket), client_id)
    , m_page_host(PageHost::create(*this))
{
    s_connections.set(client_id, *this);
    m_paint_flush_timer = Core::Timer::create_single_shot(0, [this] { flush_pending_paint_requests(); });
}

ClientConnection::~ClientConnection()
{
}

void ClientConnection::die()
{
    s_connections.remove(client_id());
    if (s_connections.is_empty())
        Core::EventLoop::current().quit(0);
}

Web::Page& ClientConnection::page()
{
    return m_page_host->page();
}

const Web::Page& ClientConnection::page() const
{
    return m_page_host->page();
}

OwnPtr<Messages::WebContentServer::GreetResponse> ClientConnection::handle(const Messages::WebContentServer::Greet& message)
{
    set_client_pid(message.client_pid());
    return make<Messages::WebContentServer::GreetResponse>(client_id(), getpid());
}

void ClientConnection::handle(const Messages::WebContentServer::UpdateSystemTheme& message)
{
    Gfx::set_system_theme(message.theme_buffer());
    auto impl = Gfx::PaletteImpl::create_with_anonymous_buffer(message.theme_buffer());
    m_page_host->set_palette_impl(*impl);
}

void ClientConnection::handle(const Messages::WebContentServer::LoadURL& message)
{
#ifdef DEBUG_SPAM
    dbg() << "handle: WebContentServer::LoadURL: url=" << message.url();
#endif
    page().load(message.url());
}

void ClientConnection::handle(const Messages::WebContentServer::LoadHTML& message)
{
#ifdef DEBUG_SPAM
    dbg() << "handle: WebContentServer::LoadHTML: html=" << message.html() << ", url=" << message.url();
#endif
    page().load_html(message.html(), message.url());
}

void ClientConnection::handle(const Messages::WebContentServer::SetViewportRect& message)
{
#ifdef DEBUG_SPAM
    dbg() << "handle: WebContentServer::SetViewportRect: rect=" << message.rect();
#endif
    m_page_host->set_viewport_rect(message.rect());
}

void ClientConnection::handle(const Messages::WebContentServer::AddBackingStore& message)
{
    m_backing_stores.set(message.backing_store_id(), *message.bitmap().bitmap());
}

void ClientConnection::handle(const Messages::WebContentServer::RemoveBackingStore& message)
{
    m_backing_stores.remove(message.backing_store_id());
}

void ClientConnection::handle(const Messages::WebContentServer::Paint& message)
{
    for (auto& pending_paint : m_pending_paint_requests) {
        if (pending_paint.bitmap_id == message.backing_store_id()) {
            pending_paint.content_rect = message.content_rect();
            return;
        }
    }

    auto it = m_backing_stores.find(message.backing_store_id());
    if (it == m_backing_stores.end()) {
        did_misbehave("Client requested paint with backing store ID");
        return;
    }

    auto& bitmap = *it->value;
    m_pending_paint_requests.append({ message.content_rect(), bitmap, message.backing_store_id() });
    m_paint_flush_timer->start();
}

void ClientConnection::flush_pending_paint_requests()
{
    for (auto& pending_paint : m_pending_paint_requests) {
        m_page_host->paint(pending_paint.content_rect, *pending_paint.bitmap);
        post_message(Messages::WebContentClient::DidPaint(pending_paint.content_rect, pending_paint.bitmap_id));
    }
    m_pending_paint_requests.clear();
}

void ClientConnection::handle(const Messages::WebContentServer::MouseDown& message)
{
    page().handle_mousedown(message.position(), message.button(), message.modifiers());
}

void ClientConnection::handle(const Messages::WebContentServer::MouseMove& message)
{
    page().handle_mousemove(message.position(), message.buttons(), message.modifiers());
}

void ClientConnection::handle(const Messages::WebContentServer::MouseUp& message)
{
    page().handle_mouseup(message.position(), message.button(), message.modifiers());
}

void ClientConnection::handle(const Messages::WebContentServer::KeyDown& message)
{
    page().handle_keydown((KeyCode)message.key(), message.modifiers(), message.code_point());
}

}
