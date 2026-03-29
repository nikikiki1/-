import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
import json
import os
from typing import Dict, Optional, Any, Union, List
from services.user_manager import UserManager
from services.product_manager import ProductManager
from services.order_manager import OrderManager
from models.user import User, Merchant, Customer
from models.product import Product
from models.order import Order

class OnlineMallGUI:
    def __init__(self, root: tk.Tk) -> None:
        self.root: tk.Tk = root
        self.root.title("网络商场系统")
        self.root.geometry("800x600")
        
        # 定义主题颜色
        self.themes: Dict[str, Dict[str, str]] = {
            "light": {
                "background": "#f0f0f0",
                "foreground": "#000000",
                "button_bg": "#e0e0e0",
                "button_fg": "#000000",  # 白色按钮用黑色文字
                "frame_bg": "#ffffff",
                "label_bg": "#f0f0f0",
                "entry_bg": "#ffffff",    # 输入框背景色
                "entry_fg": "#000000"     # 输入框文字颜色
            },
            "dark": {
                "background": "#1a1a1a",  # 更深的背景色
                "foreground": "#ffffff",  # 保持白色文字
                "button_bg": "#000000",   # 改为黑色按钮
                "button_fg": "#000000",   # 黑色按钮用黑色文字
                "frame_bg": "#2a2a2a",    # 稍亮的框架背景
                "label_bg": "#1a1a1a",    # 与背景色一致
                "entry_bg": "#000000",    # 输入框改为黑色背景
                "entry_fg": "#000000"     # 输入框文字改为黑色
            }
        }
        
        # 当前主题模式
        self.theme_mode: str = "light"
        
        # 设置中文字体和初始主题
        self.style: ttk.Style = ttk.Style()
        self.set_theme(self.theme_mode)
        
        # 配置自定义样式
        self.style.configure(
            "TButton",
            font=("SimHei", 10),
            padding=5
        )
        self.style.configure(
            "TLabel",
            font=("SimHei", 10)
        )
        self.style.configure(
            "TEntry",
            font=("SimHei", 10)
        )
        self.style.configure(
            "Header.TLabel",
            font=("SimHei", 14, "bold")
        )
        
        # 初始化各管理器
        self.user_manager: UserManager = UserManager()
        self.product_manager: ProductManager = ProductManager()
        self.order_manager: OrderManager = OrderManager(self.product_manager, self.user_manager)
        self.current_user: Optional[User] = None
        



        # 创建主容器
        self.main_frame: ttk.Frame = ttk.Frame(root)
        self.apply_theme_to_widget(self.main_frame)
        self.main_frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)
        
        # 显示登录注册界面
        self.show_login_register_screen()
    
    def clear_frame(self) -> None:
        """清空当前框架中的所有组件"""
        for widget in self.main_frame.winfo_children():
            widget.destroy()
    
    def set_theme(self, theme_mode: str) -> None:
        """设置主题"""
        self.theme_mode = theme_mode
        theme = self.themes[theme_mode]
        
        # 设置ttk样式
        # 针对夜晚模式的特殊处理
        button_fg = theme["button_fg"]
        entry_fg = theme["entry_fg"]
        
        if theme_mode == "dark":
            button_fg = "#000000"  # 夜晚模式按钮文字为黑色
            entry_fg = "#000000"  # 夜晚模式输入框文字为黑色
        
        self.style.configure(
            "TButton",
            background=theme["button_bg"],
            foreground=button_fg
        )
        self.style.configure(
            "TLabel",
            background=theme["label_bg"],
            foreground=theme["foreground"]
        )
        self.style.configure(
            "TEntry",
            fieldbackground=theme["entry_bg"],
            foreground=entry_fg
        )
        self.style.configure(
            "Header.TLabel",
            background=theme["label_bg"],
            foreground=theme["foreground"]
        )
        self.style.configure(
            "TFrame",
            background=theme["frame_bg"]
        )
        self.style.configure(
            "TLabelFrame",
            background=theme["frame_bg"],
            foreground=theme["foreground"]
        )
        
        # 更新根窗口背景
        self.root.configure(bg=theme["background"])
        
        # 如果已初始化，更新现有组件
        if hasattr(self, 'main_frame'):
            self.apply_theme_to_widget(self.main_frame)
    
    def apply_theme_to_widget(self, widget: Union[tk.Widget, ttk.Widget]) -> None:
        """递归应用主题到所有组件"""
        theme = self.themes[self.theme_mode]
        
        # 针对夜晚模式的特殊文字颜色处理
        button_fg = theme["button_fg"]
        entry_fg = theme["entry_fg"]
        
        if self.theme_mode == "dark":
            button_fg = "#000000"  # 夜晚模式按钮文字为黑色
            entry_fg = "#000000"  # 夜晚模式输入框文字为黑色
        
        # 设置当前组件的背景和前景色
        try:
            widget.configure(bg=theme["background"])  # type: ignore[call-arg]
        except:
            pass
        try:
            widget.configure(background=theme["background"])  # type: ignore[call-arg]
        except:
            pass
        
        # 区分按钮和其他组件的文字颜色
        try:
            # 如果是按钮组件，使用button_fg
            if isinstance(widget, tk.Button):
                widget.configure(fg=button_fg)
            elif isinstance(widget, ttk.Button):
                # ttk.Button不支持直接设置foreground，使用style代替
                pass
            elif isinstance(widget, tk.Entry):
                widget.configure(fg=entry_fg)
            else:
                widget.configure(fg=theme["foreground"])
        except:
            pass
            
        try:
            # 如果是按钮组件，使用button_fg
            if isinstance(widget, tk.Button):
                widget.configure(foreground=button_fg)
            elif isinstance(widget, ttk.Button):
                # ttk.Button使用样式而不是直接设置前景色
                pass
            elif isinstance(widget, tk.Entry):
                widget.configure(bg=theme["entry_bg"])
            elif not isinstance(widget, ttk.Widget):  # 跳过ttk组件的直接foreground配置
                widget.configure(foreground=theme["foreground"])
        except:
            pass
        
        # 递归应用到所有子组件
        for child in widget.winfo_children():
            self.apply_theme_to_widget(child)
    




    def toggle_theme(self) -> None:
        """切换主题模式"""
        new_theme = "dark" if self.theme_mode == "light" else "light"
        self.set_theme(new_theme)
        messagebox.showinfo("主题切换", f"已切换至{'白天' if new_theme == 'light' else '夜晚'}模式")
    
    def show_login_register_screen(self) -> None:
        """显示登录注册界面"""
        self.clear_frame()
        
        # 顶部主题切换按钮
        theme_button = ttk.Button(self.main_frame, text="切换到" + ("夜晚" if self.theme_mode == "light" else "白天") + "模式", command=self.toggle_theme)
        theme_button.pack(side=tk.TOP, anchor=tk.E, padx=20, pady=10)
        
        # 标题
        header_label = ttk.Label(self.main_frame, text="网络商场系统", style="Header.TLabel")
        header_label.pack(pady=20)
        
        # 登录框架
        login_frame = ttk.LabelFrame(self.main_frame, text="用户登录")
        self.apply_theme_to_widget(login_frame)
        login_frame.pack(fill=tk.X, pady=10, padx=50)
        
        # 登录表单
        ttk.Label(login_frame, text="用户名:").grid(row=0, column=0, padx=10, pady=10, sticky=tk.W)
        self.login_username = ttk.Entry(login_frame, width=30)
        self.login_username.grid(row=0, column=1, padx=10, pady=10)
        
        ttk.Label(login_frame, text="密码:").grid(row=1, column=0, padx=10, pady=10, sticky=tk.W)
        self.login_password = ttk.Entry(login_frame, width=30, show="*")
        self.login_password.grid(row=1, column=1, padx=10, pady=10)
        
        login_button = ttk.Button(login_frame, text="登录", command=self.login)
        login_button.grid(row=2, column=1, padx=10, pady=10, sticky=tk.E)
        
        # 注册框架
        register_frame = ttk.LabelFrame(self.main_frame, text="新用户注册")
        self.apply_theme_to_widget(register_frame)
        register_frame.pack(fill=tk.X, pady=10, padx=50)
        
        # 注册按钮
        register_customer_button = ttk.Button(register_frame, text="注册为顾客", command=lambda: self.register_dialog("customer"))
        register_customer_button.pack(side=tk.LEFT, padx=50, pady=20)
        
        register_merchant_button = ttk.Button(register_frame, text="注册为商家", command=lambda: self.register_dialog("merchant"))
        register_merchant_button.pack(side=tk.RIGHT, padx=50, pady=20)
        
        # 退出按钮
        exit_button = ttk.Button(self.main_frame, text="退出系统", command=self.root.quit)
        exit_button.pack(pady=20)
    
    def register_dialog(self, role: str) -> None:
        """注册对话框"""
        dialog = tk.Toplevel(self.root)
        dialog.title("用户注册" + ("(顾客)" if role == "customer" else "(商家)"))
        dialog.geometry("400x300")
        dialog.resizable(False, False)
        
        # 应用主题到对话框
        theme = self.themes[self.theme_mode]
        dialog.configure(bg=theme["background"])
        
        # 居中显示
        dialog.transient(self.root)
        dialog.grab_set()
        
        # 表单
        ttk.Label(dialog, text="用户名:").grid(row=0, column=0, padx=20, pady=10, sticky=tk.W)
        username_entry = ttk.Entry(dialog, width=25)
        username_entry.grid(row=0, column=1, padx=10, pady=10)
        
        ttk.Label(dialog, text="密码:").grid(row=1, column=0, padx=20, pady=10, sticky=tk.W)
        password_entry = ttk.Entry(dialog, width=25, show="*")
        password_entry.grid(row=1, column=1, padx=10, pady=10)
        
        ttk.Label(dialog, text="邮箱:").grid(row=2, column=0, padx=20, pady=10, sticky=tk.W)
        email_entry = ttk.Entry(dialog, width=25)
        email_entry.grid(row=2, column=1, padx=10, pady=10)
        
        if role == "customer":
            ttk.Label(dialog, text="收货地址:").grid(row=3, column=0, padx=20, pady=10, sticky=tk.W)
            address_entry = ttk.Entry(dialog, width=25)
            address_entry.grid(row=3, column=1, padx=10, pady=10)
            
            ttk.Label(dialog, text="手机号码:").grid(row=4, column=0, padx=20, pady=10, sticky=tk.W)
            phone_entry = ttk.Entry(dialog, width=25)
            phone_entry.grid(row=4, column=1, padx=10, pady=10)
        else:
            ttk.Label(dialog, text="店铺名称:").grid(row=3, column=0, padx=20, pady=10, sticky=tk.W)
            shop_entry = ttk.Entry(dialog, width=25)
            shop_entry.grid(row=3, column=1, padx=10, pady=10)
            
            ttk.Label(dialog, text="联系信息:").grid(row=4, column=0, padx=20, pady=10, sticky=tk.W)
            contact_entry = ttk.Entry(dialog, width=25)
            contact_entry.grid(row=4, column=1, padx=10, pady=10)
        
        def submit() -> None:
            username = username_entry.get().strip()
            password = password_entry.get().strip()
            email = email_entry.get().strip()
            
            if not username or not password or not email:
                messagebox.showerror("注册失败", "请填写所有必填字段")
                return
            
            kwargs = {}
            if role == "customer":
                address = address_entry.get().strip()
                phone = phone_entry.get().strip()
                if not address or not phone:
                    messagebox.showerror("注册失败", "请填写所有必填字段")
                    return
                kwargs['shipping_address'] = address
                kwargs['phone_number'] = phone
            else:
                shop_name = shop_entry.get().strip()
                contact_info = contact_entry.get().strip()
                if not shop_name or not contact_info:
                    messagebox.showerror("注册失败", "请填写所有必填字段")
                    return
                kwargs['shop_name'] = shop_name
                kwargs['contact_info'] = contact_info
            
            user, msg = self.user_manager.register_user(username, password, email, role, **kwargs)
            messagebox.showinfo("注册结果", msg)
            if user:
                dialog.destroy()
        
        submit_button = ttk.Button(dialog, text="注册", command=submit)
        submit_button.grid(row=5, column=1, padx=10, pady=20, sticky=tk.E)
    
    def login(self) -> None:
        """用户登录"""
        username = self.login_username.get().strip()
        password = self.login_password.get().strip()
        
        if not username or not password:
            messagebox.showerror("登录失败", "请输入用户名和密码")
            return
        
        user, msg = self.user_manager.login_user(username, password)
        if user:
            self.current_user = user
            self.show_main_menu()
        else:
            messagebox.showerror("登录失败", msg)
    
    def show_main_menu(self) -> None:
        """显示主菜单"""
        self.clear_frame()
        
        # 顶部控制区域
        top_control_frame = ttk.Frame(self.main_frame)
        top_control_frame.pack(fill=tk.X, pady=10)
        
        # 主题切换按钮
        theme_button = ttk.Button(top_control_frame, text="切换到" + ("夜晚" if self.theme_mode == "light" else "白天") + "模式", command=self.toggle_theme)
        theme_button.pack(side=tk.RIGHT, padx=10)
        
        # 欢迎信息
        if self.current_user:
            welcome_label = ttk.Label(self.main_frame, text=f"欢迎回来，{self.current_user.username}！", style="Header.TLabel")
            welcome_label.pack(pady=20)
            
            role_label = ttk.Label(self.main_frame, text=f"角色：{self.current_user.role}")
            role_label.pack(pady=5)
        
        # 功能按钮区域
        button_frame = ttk.Frame(self.main_frame)
        button_frame.pack(fill=tk.X, pady=20)
        
        if self.current_user:
            if self.current_user.role == 'merchant':
                self.show_merchant_buttons(button_frame)
            else:
                self.show_customer_buttons(button_frame)
        
        # 退出登录按钮
        logout_button = ttk.Button(self.main_frame, text="退出登录", command=self.logout)
        logout_button.pack(pady=20)
    
    def show_merchant_buttons(self, parent: ttk.Frame) -> None:
        """显示商家功能按钮"""
        buttons = [
            ("添加商品", self.add_product_dialog),
            ("管理商品", self.manage_products),
            ("查看订单", self.view_merchant_orders),
            ("处理订单", self.process_merchant_order),
            ("查看库存", self.view_inventory)
        ]
        
        for text, command in buttons:
            button = ttk.Button(parent, text=text, command=command, width=15)
            button.pack(side=tk.LEFT, padx=10, pady=10)
    
    def show_customer_buttons(self, parent: ttk.Frame) -> None:
        """显示顾客功能按钮"""
        buttons = [
            ("浏览商品", self.browse_products),
            ("搜索商品", self.search_products_dialog),
            ("购买商品", self.browse_products),
            ("查看订单", self.view_customer_orders),
            ("联系卖家", self.contact_seller_dialog)
        ]
        
        for text, command in buttons:
            button = ttk.Button(parent, text=text, command=command, width=15)
            button.pack(side=tk.LEFT, padx=10, pady=10)
    
    def logout(self) -> None:
        """用户登出"""
        if self.current_user:
            self.current_user.logout()
            self.current_user = None
            self.show_login_register_screen()
    
    # 商家功能
    def add_product_dialog(self) -> None:
        """添加商品对话框"""
        dialog = tk.Toplevel(self.root)
        dialog.title("添加商品")
        dialog.geometry("400x350")
        dialog.resizable(False, False)
        
        # 居中显示
        dialog.transient(self.root)
        dialog.grab_set()
        
        # 表单
        ttk.Label(dialog, text="商品名称:").grid(row=0, column=0, padx=20, pady=10, sticky=tk.W)
        name_entry = ttk.Entry(dialog, width=25)
        name_entry.grid(row=0, column=1, padx=10, pady=10)
        
        ttk.Label(dialog, text="商品描述:").grid(row=1, column=0, padx=20, pady=10, sticky=tk.NW)
        description_text = tk.Text(dialog, width=25, height=4)
        description_text.grid(row=1, column=1, padx=10, pady=10)
        
        ttk.Label(dialog, text="商品价格:").grid(row=2, column=0, padx=20, pady=10, sticky=tk.W)
        price_entry = ttk.Entry(dialog, width=25)
        price_entry.grid(row=2, column=1, padx=10, pady=10)
        
        ttk.Label(dialog, text="库存数量:").grid(row=3, column=0, padx=20, pady=10, sticky=tk.W)
        stock_entry = ttk.Entry(dialog, width=25)
        stock_entry.grid(row=3, column=1, padx=10, pady=10)
        
        def submit() -> None:
            name = name_entry.get().strip()
            description = description_text.get("1.0", tk.END).strip()
            price_text = price_entry.get().strip()
            stock_text = stock_entry.get().strip()
            
            if not name or not description or not price_text or not stock_text:
                messagebox.showerror("添加失败", "请填写所有必填字段")
                return
            
            try:
                price = float(price_text)
                stock = int(stock_text)
                if price <= 0 or stock < 0:
                    raise ValueError
            except ValueError:
                messagebox.showerror("添加失败", "价格必须是正数，库存必须是非负整数")
                return
            
            if self.current_user:
                product, msg = self.current_user.add_product(self.product_manager, name, description, price, stock)
            messagebox.showinfo("添加结果", msg)
            if product:
                dialog.destroy()
        
        submit_button = ttk.Button(dialog, text="添加", command=submit)
        submit_button.grid(row=4, column=1, padx=10, pady=20, sticky=tk.E)
    
    def manage_products(self) -> None:
        """管理商品"""
        if self.current_user is None:
            return
        products = self.current_user.manage_products(self.product_manager)
        
        if not products:
            messagebox.showinfo("商品管理", "您还没有添加任何商品")
            return
        
        # 创建商品列表对话框
        dialog = tk.Toplevel(self.root)
        dialog.title("商品管理")
        dialog.geometry("600x400")
        
        # 创建表格
        columns = ("id", "name", "price", "stock", "status")
        tree = ttk.Treeview(dialog, columns=columns, show="headings")
        
        # 设置列标题
        tree.heading("id", text="商品ID")
        tree.heading("name", text="商品名称")
        tree.heading("price", text="价格")
        tree.heading("stock", text="库存")
        tree.heading("status", text="状态")
        
        # 设置列宽
        tree.column("id", width=80)
        tree.column("name", width=200)
        tree.column("price", width=80)
        tree.column("stock", width=80)
        tree.column("status", width=80)
        
        # 填充数据
        for product in products:
            status = "上架" if product.is_active else "下架"
            tree.insert("", tk.END, values=(product.product_id, product.name, product.price, product.stock_quantity, status))
        
        tree.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)
        
        # 操作按钮
        def on_select() -> None:
            selected = tree.selection()
            if not selected:
                messagebox.showwarning("警告", "请先选择一个商品")
                return
            
            item_data = tree.item(selected[0])
            if item_data is None:
                return
            product_id = item_data["values"][0]
            product = self.product_manager.get_product(product_id)
            
            if product:
                self.product_management_dialog(product)
                # 刷新列表
                for item in tree.get_children():
                    tree.delete(str(item))
                if self.current_user is not None:
                    for p in self.current_user.manage_products(self.product_manager):
                        status = "上架" if p.in_stock else "下架"
                        tree.insert("", tk.END, values=(p.product_id, p.name, p.price, p.stock, status))
        
        action_button = ttk.Button(dialog, text="管理选中商品", command=on_select)
        action_button.pack(pady=10)
    
    def product_management_dialog(self, product: Any) -> None:
        """商品管理对话框"""
        dialog = tk.Toplevel(self.root)
        dialog.title(f"管理商品: {product.name}")
        dialog.geometry("400x300")
        
        # 显示商品信息
        info_frame = ttk.LabelFrame(dialog, text="商品信息")
        info_frame.pack(fill=tk.X, padx=20, pady=10)
        
        ttk.Label(info_frame, text=f"ID: {product.product_id}").grid(row=0, column=0, padx=10, pady=5, sticky=tk.W)
        ttk.Label(info_frame, text=f"名称: {product.name}").grid(row=1, column=0, padx=10, pady=5, sticky=tk.W)
        ttk.Label(info_frame, text=f"价格: {product.price}").grid(row=0, column=1, padx=10, pady=5, sticky=tk.W)
        ttk.Label(info_frame, text=f"库存: {product.stock_quantity}").grid(row=1, column=1, padx=10, pady=5, sticky=tk.W)
        
        # 操作按钮
        button_frame = ttk.Frame(dialog)
        button_frame.pack(fill=tk.X, padx=20, pady=20)
        
        ttk.Button(button_frame, text="更新商品信息", command=lambda: self.update_product_dialog(product)).pack(side=tk.LEFT, padx=10)
        
        def restock_and_refresh() -> None:
            self.restock_dialog(product)
            # 刷新商品信息显示
            # 重新获取商品信息
            refreshed_product = self.product_manager.get_product(product.product_id)
            if refreshed_product:
                # 更新库存标签
                for widget in info_frame.winfo_children():
                    if isinstance(widget, ttk.Label) and widget.cget("text").startswith("库存:"):
                        widget.config(text=f"库存: {refreshed_product.stock_quantity}")
                        break
        
        ttk.Button(button_frame, text="补充库存", command=restock_and_refresh).pack(side=tk.LEFT, padx=10)
        status_text = "下架" if product.is_active else "上架"
        ttk.Button(button_frame, text=f"{status_text}商品", command=lambda: self.toggle_product_status(product)).pack(side=tk.LEFT, padx=10)
    
    def update_product_dialog(self, product: Any) -> None:
        """更新商品信息对话框"""
        dialog = tk.Toplevel(self.root)
        dialog.title(f"更新商品: {product.name}")
        dialog.geometry("400x350")
        
        ttk.Label(dialog, text="商品名称:").grid(row=0, column=0, padx=20, pady=10, sticky=tk.W)
        name_entry = ttk.Entry(dialog, width=25)
        name_entry.insert(0, product.name)
        name_entry.grid(row=0, column=1, padx=10, pady=10)
        
        ttk.Label(dialog, text="商品描述:").grid(row=1, column=0, padx=20, pady=10, sticky=tk.NW)
        description_text = tk.Text(dialog, width=25, height=4)
        description_text.insert("1.0", product.description)
        description_text.grid(row=1, column=1, padx=10, pady=10)
        
        ttk.Label(dialog, text="商品价格:").grid(row=2, column=0, padx=20, pady=10, sticky=tk.W)
        price_entry = ttk.Entry(dialog, width=25)
        price_entry.insert(0, str(product.price))
        price_entry.grid(row=2, column=1, padx=10, pady=10)
        
        def submit() -> None:
            name = name_entry.get().strip()
            description = description_text.get("1.0", tk.END).strip()
            price_text = price_entry.get().strip()
            
            if not name or not description or not price_text:
                messagebox.showerror("更新失败", "请填写所有必填字段")
                return
            
            try:
                price = float(price_text)
                if price <= 0:
                    raise ValueError
            except ValueError:
                messagebox.showerror("更新失败", "价格必须是正数")
                return
            
            success, msg = self.product_manager.update_product(product.product_id, name=name, description=description, price=price)
            messagebox.showinfo("更新结果", msg)
            if success:
                dialog.destroy()
        
        submit_button = ttk.Button(dialog, text="更新", command=submit)
        submit_button.grid(row=3, column=1, padx=10, pady=20, sticky=tk.E)
    
    def restock_dialog(self, product: Any) -> None:
        """补充库存对话框"""
        quantity = simpledialog.askinteger("补充库存", f"请输入要补充的库存数量 (当前库存: {product.stock_quantity}):", minvalue=1)
        if quantity is not None:
            success, msg = self.product_manager.update_stock(product.product_id, quantity)
            messagebox.showinfo("补充结果", msg)
    
    def toggle_product_status(self, product: Any) -> None:
        """切换商品上架/下架状态"""
        if product.is_active:
            success, msg = self.product_manager.toggle_product_status(product.product_id, activate=False)
        else:
            success, msg = self.product_manager.toggle_product_status(product.product_id, activate=True)
        messagebox.showinfo("操作结果", msg)
    
    def view_merchant_orders(self) -> None:
        """查看商家订单"""
        if self.current_user is None:
            return
        orders = self.order_manager.get_orders_by_merchant(self.current_user.user_id)
        
        if not orders:
            messagebox.showinfo("订单列表", "您还没有收到任何订单")
            return
        
        # 创建订单列表对话框
        dialog = tk.Toplevel(self.root)
        dialog.title("我的订单")
        dialog.geometry("700x400")
        
        # 应用主题到对话框
        theme = self.themes[self.theme_mode]
        dialog.configure(bg=theme["background"])
        
        # 创建表格
        columns = ("id", "customer", "product", "quantity", "price", "total", "status", "date")
        tree = ttk.Treeview(dialog, columns=columns, show="headings")
        
        # 设置Treeview样式以支持主题
        try:
            # 增强Treeview的对比度，特别是夜晚模式下
            tree_bg = theme["frame_bg"]
            tree_fg = theme["foreground"]
            heading_bg = theme["button_bg"]
            heading_fg = theme["foreground"]
            
            # 确保在夜晚模式下有更好的对比度
            if self.theme_mode == "dark":
                heading_bg = "#5a5a5a"  # 更亮的表头背景
                
            self.style.configure("Treeview", 
                                background=tree_bg,
                                foreground=tree_fg,
                                fieldbackground=tree_bg,
                                bordercolor="#666666",
                                borderwidth=1)
            self.style.configure("Treeview.Heading",
                                background=heading_bg,
                                foreground=heading_fg,
                                font=("SimHei", 10, "bold"),  # 加粗表头文字
                                bordercolor="#666666",
                                borderwidth=1)
            # 设置选中行的样式
            self.style.map("Treeview", 
                          background=[("selected", "#0066cc")],
                          foreground=[("selected", "#ffffff")])
        except:
            pass
        
        # 设置列标题和宽度
        tree.heading("id", text="订单ID")
        tree.column("id", width=80)
        tree.heading("customer", text="顾客")
        tree.column("customer", width=100)
        tree.heading("product", text="商品")
        tree.column("product", width=150)
        tree.heading("quantity", text="数量")
        tree.column("quantity", width=60)
        tree.heading("price", text="单价")
        tree.column("price", width=60)
        tree.heading("total", text="总价")
        tree.column("total", width=80)
        tree.heading("status", text="状态")
        tree.column("status", width=80)
        tree.heading("date", text="日期")
        tree.column("date", width=120)
        
        # 填充数据
        for order in orders:
            product = self.product_manager.get_product(order.product_id)
            customer = self.user_manager.get_user_by_id(order.customer_id)
            tree.insert("", tk.END, values=(
                order.order_id,
                customer.username if customer else "未知",
                product.name if product else "未知",
                order.quantity,
                order.price,
                order.total_amount,
                self.get_order_status_text(order.status),
                order.order_date
            ))
        
        tree.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)
        
        # 查看联系方式按钮
        def view_contact() -> None:
            selected = tree.selection()
            if not selected:
                messagebox.showwarning("警告", "请先选择一个订单")
                return
            
            item = tree.item(selected[0])
            if item is None:
                return
            order_id = item["values"][0]
            order = self.order_manager.get_order(order_id)
            
            if order and order.status in ["accepted", "completed"]:
                contact_info, msg = self.order_manager.exchange_contact_info(order_id)
                if contact_info:
                    messagebox.showinfo("顾客联系方式", f"顾客信息:\n用户名: {contact_info['customer']['name']}\n电话: {contact_info['customer']['phone']}\n地址: {contact_info['customer']['address']}\n\n您的联系方式:\n用户名: {contact_info['merchant']['name']}\n店铺: {contact_info['merchant']['shop']}\n联系方式: {contact_info['merchant']['contact']}")
                else:
                    messagebox.showerror("错误", msg)
            else:
                messagebox.showinfo("提示", "只有已接受或已完成的订单才能查看联系方式")
        
        button_frame = ttk.Frame(dialog)
        button_frame.pack(fill=tk.X, padx=20, pady=10)
        
        ttk.Button(button_frame, text="查看顾客联系方式", command=view_contact).pack(side=tk.LEFT, padx=10)
    
    def process_merchant_order(self) -> None:
        """处理商家订单"""
        if self.current_user is None:
            return
        orders = self.order_manager.get_orders_by_merchant(self.current_user.user_id)
        pending_orders = [o for o in orders if o.status == "pending"]
        
        if not pending_orders:
            messagebox.showinfo("订单处理", "没有待处理的订单")
            return
        
        # 创建订单选择对话框
        dialog = tk.Toplevel(self.root)
        dialog.title("处理订单")
        dialog.geometry("700x400")
        
        # 创建表格
        columns = ("id", "customer", "product", "quantity", "total", "date")
        tree = ttk.Treeview(dialog, columns=columns, show="headings")
        
        # 设置列标题和宽度
        tree.heading("id", text="订单ID")
        tree.column("id", width=100)
        tree.heading("customer", text="顾客")
        tree.column("customer", width=100)
        tree.heading("product", text="商品")
        tree.column("product", width=200)
        tree.heading("quantity", text="数量")
        tree.column("quantity", width=80)
        tree.heading("total", text="总价")
        tree.column("total", width=80)
        tree.heading("date", text="日期")
        tree.column("date", width=120)
        
        # 填充数据
        for order in pending_orders:
            product = self.product_manager.get_product(order.product_id)
            customer = self.user_manager.get_user_by_id(order.customer_id)
            tree.insert("", tk.END, values=(
                order.order_id,
                customer.username if customer else "未知",
                product.name if product else "未知",
                order.quantity,
                order.total_amount,
                order.order_date
            ))
        
        tree.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)
        
        # 操作按钮
        def accept_order() -> None:
            selected = tree.selection()
            if not selected:
                messagebox.showwarning("警告", "请先选择一个订单")
                return
            
            item = tree.item(selected[0])
            if item is None:
                return
            order_id = item["values"][0]
            success, msg = self.order_manager.process_order(order_id, "accept")
            messagebox.showinfo("操作结果", msg)
            if success:
                dialog.destroy()
        
        def cancel_order() -> None:
            selected = tree.selection()
            if not selected:
                messagebox.showwarning("警告", "请先选择一个订单")
                return
            
            item = tree.item(selected[0])
            if item is None:
                return
            order_id = item["values"][0]
            reason = simpledialog.askstring("取消订单", "请输入取消原因:")
            if reason is not None:
                success, msg = self.order_manager.process_order(order_id, "cancel")
                messagebox.showinfo("操作结果", msg)
                if success:
                    dialog.destroy()
        
        button_frame = ttk.Frame(dialog)
        button_frame.pack(fill=tk.X, padx=20, pady=10)
        
        ttk.Button(button_frame, text="接受订单", command=accept_order).pack(side=tk.LEFT, padx=20)
        ttk.Button(button_frame, text="取消订单", command=cancel_order).pack(side=tk.LEFT, padx=20)
    
    def view_inventory(self) -> None:
        """查看库存"""
        # 直接使用product_manager的方法获取商家的商品
        if self.current_user is None:
            return
        products = self.product_manager.get_products_by_merchant(self.current_user.merchant_id)
        
        if not products:
            messagebox.showinfo("库存管理", "您还没有添加任何商品")
            return
        
        # 创建库存列表对话框
        dialog = tk.Toplevel(self.root)
        dialog.title("库存管理")
        dialog.geometry("500x300")
        
        # 创建表格
        columns = ("name", "stock", "status")
        tree = ttk.Treeview(dialog, columns=columns, show="headings")
        
        # 设置列标题和宽度
        tree.heading("name", text="商品名称")
        tree.column("name", width=200)
        tree.heading("stock", text="库存数量")
        tree.column("stock", width=100)
        tree.heading("status", text="状态")
        tree.column("status", width=100)
        
        # 填充数据
        for product in products:
            status = "充足" if product.stock_quantity > 10 else "低库存" if product.stock_quantity > 0 else "缺货"
            tree.insert("", tk.END, values=(product.name, product.stock_quantity, status))
        
        tree.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)
    
    # 顾客功能
    def browse_products(self) -> None:
        """浏览商品"""
        products = self.product_manager.get_all_products()
        




        if not products:
            messagebox.showinfo("商品列表", "暂无商品")
            return
        
        # 创建商品列表对话框
        dialog = tk.Toplevel(self.root)
        dialog.title("商品列表")
        dialog.geometry("700x500")
        
        # 应用主题到对话框
        theme = self.themes[self.theme_mode]
        dialog.configure(bg=theme["background"])
        
        # 创建表格
        columns = ("id", "name", "merchant", "price", "stock", "status")
        tree = ttk.Treeview(dialog, columns=columns, show="headings")
        
        # 设置Treeview样式以支持主题
        try:
            self.style.configure("Treeview", 
                                background=theme["frame_bg"],
                                foreground=theme["foreground"],
                                fieldbackground=theme["frame_bg"])
            self.style.configure("Treeview.Heading",
                                background=theme["button_bg"],
                                foreground=theme["foreground"])
        except:
            pass
        
        # 设置列标题和宽度
        tree.heading("id", text="商品ID")
        tree.column("id", width=80)
        tree.heading("name", text="商品名称")
        tree.column("name", width=200)
        tree.heading("merchant", text="商家")
        tree.column("merchant", width=120)
        tree.heading("price", text="价格")
        tree.column("price", width=80)
        tree.heading("stock", text="库存")
        tree.column("stock", width=80)
        tree.heading("status", text="状态")
        tree.column("status", width=80)
        
        # 填充数据
        for product in products:
            if product.is_active:
                merchant = self.user_manager.get_user_by_id(product.merchant_id)
                tree.insert("", tk.END, values=(
                    product.product_id,
                    product.name,
                    merchant.shop_name if merchant else "未知",
                    product.price,
                    product.stock_quantity,
                    "可购买"
                ))
        
        tree.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)
        
        # 购买按钮
        def purchase() -> None:
            selected = tree.selection()
            if not selected:
                messagebox.showwarning("警告", "请先选择一个商品")
                return
            
            item = tree.item(selected[0])
            if item is None:
                return
            product_id = item["values"][0]
            product = self.product_manager.get_product(product_id)
            
            if product:
                self.purchase_dialog(product)
        
        purchase_button = ttk.Button(dialog, text="购买选中商品", command=purchase)
        purchase_button.pack(pady=10)
    
    def search_products_dialog(self) -> None:
        """搜索商品对话框"""
        dialog = tk.Toplevel(self.root)
        dialog.title("搜索商品")
        dialog.geometry("400x200")
        
        ttk.Label(dialog, text="搜索关键词:").grid(row=0, column=0, padx=20, pady=20, sticky=tk.W)
        keyword_entry = ttk.Entry(dialog, width=25)
        keyword_entry.grid(row=0, column=1, padx=10, pady=20)
        
        def search() -> None:
            keyword = keyword_entry.get().strip()
            if not keyword:
                messagebox.showwarning("警告", "请输入搜索关键词")
                return
            
            products = self.product_manager.search_products(keyword)
            
            # 显示搜索结果
            result_dialog = tk.Toplevel(self.root)
            result_dialog.title(f"搜索结果: {keyword}")
            result_dialog.geometry("700x400")
            
            # 创建表格
            columns = ("id", "name", "merchant", "price", "stock")
            tree = ttk.Treeview(result_dialog, columns=columns, show="headings")
            
            # 设置列标题和宽度
            tree.heading("id", text="商品ID")
            tree.column("id", width=80)
            tree.heading("name", text="商品名称")
            tree.column("name", width=200)
            tree.heading("merchant", text="商家")
            tree.column("merchant", width=120)
            tree.heading("price", text="价格")
            tree.column("price", width=80)
            tree.heading("stock", text="库存")
            tree.column("stock", width=80)
            
            # 填充数据
            for product in products:
                if product.is_active:
                    merchant = self.user_manager.get_user_by_id(product.merchant_id)
                    tree.insert("", tk.END, values=(
                        product.product_id,
                        product.name,
                        merchant.shop_name if merchant else "未知",
                        product.price,
                        product.stock_quantity
                    ))
            
            tree.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)
            
            # 购买按钮
            def purchase_result() -> None:
                selected = tree.selection()
                if not selected:
                    messagebox.showwarning("警告", "请先选择一个商品")
                    return
                
                item = tree.item(selected[0])
                if item is None:
                    return
                product_id = item["values"][0]
                product = self.product_manager.get_product(product_id)
                
                if product:
                    self.purchase_dialog(product)
            
            if products:
                purchase_button = ttk.Button(result_dialog, text="购买选中商品", command=purchase_result)
                purchase_button.pack(pady=10)
            else:
                ttk.Label(result_dialog, text="没有找到相关商品").pack(pady=50)
            
            dialog.destroy()
        
        search_button = ttk.Button(dialog, text="搜索", command=search)
        search_button.grid(row=1, column=1, padx=10, pady=20, sticky=tk.E)
    
    def purchase_dialog(self, product: Any) -> None:
        """购买商品对话框"""
        dialog = tk.Toplevel(self.root)
        dialog.title(f"购买: {product.name}")
        dialog.geometry("400x350")
        
        # 商品信息
        info_frame = ttk.LabelFrame(dialog, text="商品信息")
        info_frame.pack(fill=tk.X, padx=20, pady=10)
        
        ttk.Label(info_frame, text=f"名称: {product.name}").grid(row=0, column=0, padx=10, pady=5, sticky=tk.W, columnspan=2)
        ttk.Label(info_frame, text=f"价格: ¥{product.price}").grid(row=1, column=0, padx=10, pady=5, sticky=tk.W)
        ttk.Label(info_frame, text=f"库存: {product.stock_quantity}").grid(row=1, column=1, padx=10, pady=5, sticky=tk.W)
        
        # 购买数量
        ttk.Label(dialog, text="购买数量:").pack(padx=20, pady=10, anchor=tk.W)
        
        quantity_var = tk.IntVar(value=1)
        quantity_frame = ttk.Frame(dialog)
        quantity_frame.pack(padx=20, fill=tk.X)
        
        ttk.Button(quantity_frame, text="-", command=lambda: quantity_var.set(max(1, quantity_var.get() - 1))).pack(side=tk.LEFT, padx=5)
        ttk.Label(quantity_frame, textvariable=quantity_var, width=10).pack(side=tk.LEFT, padx=5)
        ttk.Button(quantity_frame, text="+", command=lambda: quantity_var.set(min(product.stock_quantity, quantity_var.get() + 1))).pack(side=tk.LEFT, padx=5)
        
        # 确认购买
        def confirm() -> None:
            quantity = quantity_var.get()




            if quantity > product.stock_quantity:
                messagebox.showerror("购买失败", "库存不足")
                return
            
            if self.current_user is None:
                return
            order, msg = self.order_manager.create_order(self.current_user.user_id, product.product_id, quantity)
            messagebox.showinfo("购买结果", msg)
            if order:
                dialog.destroy()
        
        ttk.Button(dialog, text="确认购买", command=confirm).pack(pady=30)
    
    def view_customer_orders(self) -> None:
        """查看顾客订单"""
        if self.current_user is None:
            return
        orders = self.order_manager.get_orders_by_customer(self.current_user.user_id)
        
        if not orders:
            messagebox.showinfo("我的订单", "您还没有任何订单")
            return
        
        # 创建订单列表对话框
        dialog = tk.Toplevel(self.root)
        dialog.title("我的订单")
        dialog.geometry("700x400")
        
        # 创建表格
        columns = ("id", "product", "merchant", "quantity", "price", "total", "status", "date")
        tree = ttk.Treeview(dialog, columns=columns, show="headings")
        
        # 设置列标题和宽度
        tree.heading("id", text="订单ID")
        tree.column("id", width=80)
        tree.heading("product", text="商品")
        tree.column("product", width=150)
        tree.heading("merchant", text="商家")
        tree.column("merchant", width=100)
        tree.heading("quantity", text="数量")
        tree.column("quantity", width=60)
        tree.heading("price", text="单价")
        tree.column("price", width=60)
        tree.heading("total", text="总价")
        tree.column("total", width=80)
        tree.heading("status", text="状态")
        tree.column("status", width=80)
        tree.heading("date", text="日期")
        tree.column("date", width=120)
        
        # 填充数据
        for order in orders:
            product = self.product_manager.get_product(order.product_id)
            merchant = self.user_manager.get_user_by_id(product.merchant_id) if product else None
            tree.insert("", tk.END, values=(
                order.order_id,
                product.name if product else "未知",
                merchant.username if merchant else "未知",
                order.quantity,
                product.price if product else 0,
                order.total_amount,
                self.get_order_status_text(order.status),
                order.order_date
            ))
        
        tree.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)
        
        # 查看联系方式按钮
        def view_contact() -> None:
            selected = tree.selection()
            if not selected:
                messagebox.showwarning("警告", "请先选择一个订单")
                return
            
            item = tree.item(selected[0])
            if item is None:
                return
            order_id = item["values"][0]
            order = self.order_manager.get_order(order_id)
            
            if order and order.status in ["accepted", "completed"]:
                contact_info, msg = self.order_manager.exchange_contact_info(order_id)
                if contact_info:
                    messagebox.showinfo("联系方式", f"您的联系方式:\n用户名: {contact_info['customer']['name']}\n电话: {contact_info['customer']['phone']}\n地址: {contact_info['customer']['address']}\n\n商家联系方式:\n用户名: {contact_info['merchant']['name']}\n店铺: {contact_info['merchant']['shop']}\n联系方式: {contact_info['merchant']['contact']}")
                else:
                    messagebox.showerror("错误", msg)
            else:
                messagebox.showinfo("提示", "只有已接受或已完成的订单才能查看联系方式")
        
        # 确认收货按钮
        def confirm_receipt() -> None:
            selected = tree.selection()
            if not selected:
                messagebox.showwarning("警告", "请先选择一个订单")
                return
            
            item = tree.item(selected[0])
            if item is None:
                return
            order_id = item["values"][0]
            order = self.order_manager.get_order(order_id)
            
            if order and order.status == "accepted":
                if messagebox.askyesno("确认收货", "确认已收到商品吗？"):
                    success, msg = self.order_manager.complete_order(order_id)
                    messagebox.showinfo("操作结果", msg)
                    if success:
                        dialog.destroy()
            else:
                messagebox.showinfo("提示", "只有已接受的订单才能确认收货")
        
        button_frame = ttk.Frame(dialog)
        button_frame.pack(fill=tk.X, padx=20, pady=10)
        
        ttk.Button(button_frame, text="查看联系方式", command=view_contact).pack(side=tk.LEFT, padx=10)
        ttk.Button(button_frame, text="确认收货", command=confirm_receipt).pack(side=tk.LEFT, padx=10)
    
    def contact_seller_dialog(self) -> None:
        """联系卖家对话框"""
        if self.current_user is None:
            return
        # 获取用户有订单的商家列表
        orders = self.order_manager.get_orders_by_customer(self.current_user.user_id)
        merchant_ids = set(order.product_id for order in orders if order.status in ["accepted", "completed"])
        
        if not merchant_ids:
            messagebox.showinfo("联系卖家", "您还没有可以联系的卖家")
            return
        
        # 创建商家选择对话框
        dialog = tk.Toplevel(self.root)
        dialog.title("选择要联系的卖家")
        dialog.geometry("400x300")
        
        # 创建列表
        listbox = tk.Listbox(dialog, width=50, height=10)
        listbox.pack(fill=tk.BOTH, expand=True, padx=20, pady=10)
        
        merchant_list: List[Merchant] = []
        for product_id in merchant_ids:
            product = self.product_manager.get_product(product_id)
            if product:
                merchant = self.user_manager.get_user_by_id(product.merchant_id)
                if merchant and isinstance(merchant, Merchant):
                    merchant_list.append(merchant)
                    listbox.insert(tk.END, f"{merchant.username} - {merchant.shop_name}")  # type: ignore
        
        # 查看联系方式按钮
        def view_contact() -> None:
            selection = listbox.curselection()  # type: ignore
            if not selection:
                messagebox.showwarning("警告", "请先选择一个卖家")
                return
            
            merchant = merchant_list[selection[0]]
            messagebox.showinfo("联系方式", f"卖家: {merchant.username}\n店铺: {merchant.shop_name}\n联系方式: {merchant.contact_info}")
        
        ttk.Button(dialog, text="查看联系方式", command=view_contact).pack(pady=10)
    
    def get_order_status_text(self, status: str) -> str:
        """获取订单状态的中文文本"""
        status_map = {
            "pending": "待处理",
            "accepted": "已接受",
            "completed": "已完成",
            "cancelled": "已取消"
        }
        return status_map.get(status, "未知")

if __name__ == "__main__":
    root = tk.Tk()
    app = OnlineMallGUI(root)
    root.mainloop()